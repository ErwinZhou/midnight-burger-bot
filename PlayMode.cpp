#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <algorithm>
#include <iostream>

GLuint burger_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > burger_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("burger.pnct"));
	burger_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > burger_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("burger.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = burger_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = burger_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

namespace {

std::vector<Scene::Transform *> find_bins(Scene &scene) {
	std::vector<Scene::Transform *> bins;
	for (Scene::Transform &transform : scene.transforms) {
		if (transform.name.starts_with("IngredientBin.")) bins.push_back(&transform);
	}
	// Sort numeric suffixes without assuming scene export order.
	std::sort(bins.begin(), bins.end(), [](Scene::Transform const *a, Scene::Transform const *b) {
		if (a->name.size() != b->name.size()) return a->name.size() < b->name.size();
		return a->name < b->name;
	});
	if (bins.empty()) throw std::runtime_error("Scene contains no IngredientBin.N objects.");
	for (size_t i = 0; i < bins.size(); ++i) {
		std::string expected = "IngredientBin." + std::to_string(i + 1);
		if (bins[i]->name != expected) {
			throw std::runtime_error("Expected " + expected + ", found " + bins[i]->name + ". Bin numbers must be unique and consecutive.");
		}
	}
	// The current input design supports keys 1-6; this is not a supply-logic limit.
	if (bins.size() != 6) throw std::runtime_error("The six-key controls require 6 scene bins; found " + std::to_string(bins.size()));
	return bins;
}

} // namespace

PlayMode::PlayMode() : scene(*burger_scene), bin_transforms(find_bins(scene)), game(bin_transforms.size()) {
	//get pointers to arm joints for animation:
	for (auto &transform : scene.transforms) {
		if (transform.name == "ArmYaw") arm_yaw = &transform;
		else if (transform.name == "ArmShoulder") arm_shoulder = &transform;
		else if (transform.name == "ArmForearm") arm_forearm = &transform;
	}
	if (arm_yaw == nullptr) throw std::runtime_error("ArmYaw not found.");
	if (arm_shoulder == nullptr) throw std::runtime_error("ArmShoulder not found.");
	if (arm_forearm == nullptr) throw std::runtime_error("ArmForearm not found.");

	arm_yaw_base_rotation = arm_yaw->rotation;
	arm_shoulder_base_rotation = arm_shoulder->rotation;
	arm_forearm_base_rotation = arm_forearm->rotation;

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));

	camera = &scene.cameras.front();
	SDL_SetWindowRelativeMouseMode(Mode::window, false);

	{ // Cache the original ingredient appearances before hiding their drawables.
		constexpr std::array<char const *, 10> meshes = {
			"BunBottom", "Patty", "Lettuce", "CheeseSlice", "BunTop",
			"TomatoSlice", "OnionRing", "PickleSlice", "BaconStrip", "SauceBlob"
		};
		constexpr std::array<char const *, 10> names = {
			"BOTTOM BUN", "PATTY", "LETTUCE", "CHEESE", "TOP BUN",
			"TOMATO", "ONION", "PICKLE", "BACON", "SAUCE"
		};
		for (size_t i = 0; i < meshes.size(); ++i) {
			Scene::Drawable *prototype = nullptr;
			for (Scene::Drawable &drawable : scene.drawables) {
				if (drawable.transform->name == meshes[i]) prototype = &drawable;
			}
			if (!prototype) throw std::runtime_error(std::string("Missing ingredient drawable: ") + meshes[i]);
			Mesh const &mesh = burger_meshes->lookup(meshes[i]);
			IngredientLook &look = ingredient_looks[i];
			look.name = names[i];
			look.pipeline = prototype->pipeline;
			look.min = mesh.min;
			look.max = mesh.max;
			glm::vec3 extent = mesh.max - mesh.min;
			if (!(extent.x > 0 && extent.y > 0 && extent.z > 0)) {
				throw std::runtime_error(std::string("Invalid ingredient bounds: ") + meshes[i]);
			}
			// Interior width/depth, with clearance from the walls.
			float fit = std::min(1.0f, std::min(1.04f / extent.x, 0.84f / extent.y));
			look.bin_scale = glm::vec3(fit, fit, 1.0f);
			look.grab_offset = glm::vec3(0.0f, 0.0f, mesh.max.z);
			prototype->pipeline.count = 0;
		}
	}

	for (Scene::Transform &transform : scene.transforms) {
		if (transform.name == "Plate") plate = &transform;
		if (transform.name == "RecipeBoard") recipe_board = &transform;
	}
	if (!recipe_board) throw std::runtime_error("RecipeBoard not found.");
	// More readable orders without changing the source asset.
	recipe_board->scale *= glm::vec3(1.25f, 1.0f, 1.25f);
	if (!plate) throw std::runtime_error("Plate not found.");
	plate_height = burger_meshes->lookup("Plate").max.z;

	auto make_instance = [&](Scene::Transform *parent, std::string const &name) {
		scene.transforms.emplace_back();
		Scene::Transform *transform = &scene.transforms.back();
		transform->name = name;
		transform->parent = parent;
		scene.drawables.emplace_back(transform);
		scene.drawables.back().pipeline = ingredient_looks.front().pipeline;
		scene.drawables.back().pipeline.count = 0;
		return Instance{transform, &scene.drawables.back()};
	};
	{ // Allocate visible bins plus one incoming row; no per-frame scene allocations.
		for (size_t i = 0; i < bin_transforms.size(); ++i) {
			Scene::Drawable *drawable = nullptr;
			for (Scene::Drawable &candidate : scene.drawables) {
				if (candidate.transform == bin_transforms[i]) drawable = &candidate;
			}
			if (!drawable) throw std::runtime_error("Missing drawable: " + bin_transforms[i]->name);
			bin_pool.push_back({bin_transforms[i], drawable});
		}
		for (size_t i = 0; i < bin_transforms.size(); ++i) {
			Instance bin = make_instance(bin_transforms[i]->parent, "IncomingBin." + std::to_string(i));
			bin.transform->position = bin_transforms[i]->position;
			bin.transform->rotation = bin_transforms[i]->rotation;
			bin.transform->scale = bin_transforms[i]->scale;
			bin.drawable->pipeline = bin_pool[i].drawable->pipeline;
			bin.drawable->pipeline.count = 0;
			bin_pool.push_back(bin);
		}
		for (size_t i = 0; i < bin_pool.size(); ++i) {
			supply_pool.push_back(make_instance(bin_pool[i].transform, "Supply." + std::to_string(i)));
		}
		for (size_t i = 0; i < stack_pool.size(); ++i) {
			stack_pool[i] = make_instance(plate, "Stack." + std::to_string(i));
		}
	}
	clear_stack();
	sync_supplies();
	std::cout << "Burger seed: " << game.seed << std::endl;
}

PlayMode::~PlayMode() {
}

void PlayMode::sync_supplies() {
	assert(game.bins.size() == bin_transforms.size());
	for (size_t i = 0; i < supply_pool.size(); ++i) {
		Instance &instance = supply_pool[i];
		if (i >= game.bins.size()) {
			instance.drawable->pipeline.count = 0;
			continue;
		}
		IngredientLook const &look = ingredient_looks.at(static_cast<size_t>(game.bins[i]));
		instance.drawable->pipeline = look.pipeline;
		instance.transform->scale = look.bin_scale;
		glm::vec3 center = 0.5f * (look.min + look.max);
		instance.transform->position = glm::vec3(-center.x * look.bin_scale.x,
			-center.y * look.bin_scale.y, 0.12f - look.min.z * look.bin_scale.z);
	}
}

void PlayMode::clear_stack() {
	for (Instance &instance : stack_pool) instance.drawable->pipeline.count = 0;
	stack_count = 0;
	stack_height = plate_height;
}

void PlayMode::restart_game() {
	game.reset_run(std::random_device{}());
	phase = Phase::Ready;
	selected_slot = -1;
	feedback_time = 0.0f;
	arm_yaw->rotation = arm_yaw_base_rotation;
	arm_shoulder->rotation = arm_shoulder_base_rotation;
	arm_forearm->rotation = arm_forearm_base_rotation;
	clear_stack();
	sync_supplies();
	std::cout << "Burger seed: " << game.seed << std::endl;
}

void PlayMode::pick_selected() {
	if (selected_slot < 0) {
		return;
	}
	if (!game.begin_pick(static_cast<size_t>(selected_slot))) return;
	burger::PendingPick const &pick = *game.pending;
	supply_pool[pick.slot].drawable->pipeline.count = 0;
	if (pick.outcome == burger::PickOutcome::Wrong) {
		clear_stack();
	} else {
		assert(stack_count < stack_pool.size());
		IngredientLook const &look = ingredient_looks.at(static_cast<size_t>(pick.ingredient));
		Instance &instance = stack_pool[stack_count++];
		instance.drawable->pipeline = look.pipeline;
		instance.transform->scale = look.stack_scale;
		glm::vec3 center = 0.5f * (look.min + look.max);
		instance.transform->position = glm::vec3(-center.x * look.stack_scale.x,
			-center.y * look.stack_scale.y, stack_height - look.min.z * look.stack_scale.z);
		stack_height += (look.max.z - look.min.z) * look.stack_scale.z;
	}
	phase = Phase::Feedback;
	feedback_time = pick.outcome == burger::PickOutcome::Completed ? 0.9f : 0.3f;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &) {
	if (evt.type != SDL_EVENT_KEY_DOWN || evt.key.repeat) return false;
	if (evt.key.key == SDLK_ESCAPE) {
		SDL_Event quit{};
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
		return true;
	}
	if (evt.key.key == SDLK_R) {
		restart_game();
		return true;
	}
	if (phase != Phase::Ready) return true;
	if (evt.key.key >= SDLK_1 && evt.key.key <= SDLK_6) {
		selected_slot = static_cast<int>(evt.key.key - SDLK_1);
		return true;
	}
	if (evt.key.key == SDLK_RETURN || evt.key.key == SDLK_KP_ENTER) {
		pick_selected();
		return true;
	}
	return false;
}

void PlayMode::update(float elapsed) {
	if (phase != Phase::Feedback) return;
	feedback_time -= elapsed;
	if (feedback_time > 0.0f) return;
	assert(game.pending);
	if (game.pending->outcome == burger::PickOutcome::Completed) clear_stack();
	bool committed = game.commit_advance();
	assert(committed && "Feedback must finish a pending pick.");
	(void)committed;
	sync_supplies();
	selected_slot = -1;
	phase = Phase::Ready;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	GL_ERRORS(); //print any errors produced by this setup code

	scene.draw(*camera);



	{ // Order text lies on the green panel, in RecipeBoard local coordinates.
		glm::mat4 clip_from_world = camera->make_projection() * glm::mat4(camera->transform->make_local_from_world());
		DrawLines lines(clip_from_world * glm::mat4(recipe_board->make_world_from_local()));
		auto text = [&](std::string const &label, float x, float z, float height, glm::u8vec4 color) {
			lines.draw_text(label, glm::vec3(x,-0.116f,z), glm::vec3(height,0,0),
				glm::vec3(0,0,height), color);
		};
		glm::u8vec4 ink(24,48,44,255), done(28,65,46,255), current(255,247,205,255);
		text("ORDER", -1.14f, 1.80f, 0.19f, ink);
		size_t progress = game.order.next;
		if (game.pending) {
			progress = game.pending->outcome == burger::PickOutcome::Wrong ? 0 : progress + 1;
		}
		for (size_t i = 0; i < game.order.layers.size(); ++i) {
			std::string label = std::to_string(i+1) + ". " +
				ingredient_looks.at(static_cast<size_t>(game.order.layers[i])).name;
			text(label, i < 5 ? -1.14f : 0.08f, 1.56f-float(i%5)*0.19f, 0.16f,
				i < progress ? done : (i == progress ? current : ink));
		}
		std::string status;
		if (phase == Phase::Feedback) {
			status = game.pending->outcome == burger::PickOutcome::Wrong ? "WRONG - TRY AGAIN" :
				(game.pending->outcome == burger::PickOutcome::Completed ? "ORDER COMPLETE!" : "CORRECT");
		}
		text(status, -1.14f, 0.56f, 0.10f, ink);
	}
	{ // Right-aligned controls stay outside the order board.
		glDisable(GL_DEPTH_TEST);
		float aspect = camera->aspect;
		float height = std::min(0.04f, aspect / 20.0f);
		DrawLines lines(glm::mat4(1.0f/aspect,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1));
		std::array<std::string, 2> hints = {"1-6 SELECT / ENTER PICK", "R RESTART / ESC QUIT"};
		for (size_t i = 0; i < hints.size(); ++i) {
			size_t first = lines.attribs.size();
			glm::vec3 end;
			lines.draw_text(hints[i], glm::vec3(0,-0.88f-float(i)*0.07f,0),
				glm::vec3(height,0,0), glm::vec3(0,height,0), glm::u8vec4(245,245,230,255), &end);
			for (size_t v = first; v < lines.attribs.size(); ++v) {
				lines.attribs[v].Position.x += aspect - 0.06f - end.x;
			}
		}
	}
	GL_ERRORS();
}
