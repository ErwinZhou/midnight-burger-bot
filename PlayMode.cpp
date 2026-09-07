#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "load_save_png.hpp"

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
	// sort numeric suffixes without assuming scene export order
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
	// current input design supports keys 1-6
	// fof this is no supply logic
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
		else if (transform.name == "ArmWrist") arm_wrist = &transform;
		else if (transform.name == "GripperPalm") gripper = &transform;
		else if (transform.name == "GripperFinger.L") finger_left = &transform;
		else if (transform.name == "GripperFinger.R") finger_right = &transform;
		else if (transform.name == "TrashBin") trash = &transform;
	}
	if (arm_yaw == nullptr) throw std::runtime_error("ArmYaw not found.");
	if (arm_shoulder == nullptr) throw std::runtime_error("ArmShoulder not found.");
	if (arm_forearm == nullptr) throw std::runtime_error("ArmForearm not found.");

	if (!arm_wrist || !gripper || !finger_left || !finger_right || !trash) {
		throw std::runtime_error("Missing wrist, gripper, fingers or TrashBin.");
	}
	if (arm_shoulder->parent != arm_yaw || !arm_forearm->parent ||
		arm_forearm->parent->parent != arm_shoulder || arm_wrist->parent != arm_forearm ||
		gripper->parent != arm_wrist) throw std::runtime_error("Unexpected arm hierarchy.");
	// center the base on the supply row so both ends are within reach
	arm_yaw->parent->position.x = 0.5f * (bin_transforms.front()->position.x + bin_transforms.back()->position.x);
	upper_length = glm::length(arm_forearm->position);
	lower_length = glm::length(arm_wrist->position);
	upper_rest_angle = 2.0f * std::atan2(arm_forearm->parent->rotation.y, arm_forearm->parent->rotation.w);
	gripper->scale = glm::vec3(0.65f);
	finger_left_home = finger_left->position;
	finger_right_home = finger_right->position;
	// the discard bin needs to be outside the counter and inside the arm's reach
	trash->position = glm::vec3(3.0f, -3.1f, 0.0f);
	glm::vec3 shoulder_world = arm_shoulder->make_world_from_local()[3];
	travel_center = shoulder_world + glm::vec3(0.0f, 0.5f, 1.45f);
	apply_arm(angles_for(travel_center));
	set_fingers(0.0f);

	//get pointer to camera for convenience
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));

	camera = &scene.cameras.front();
	camera_fovy = camera->fovy;
	SDL_SetWindowRelativeMouseMode(Mode::window, false);

	{ // cache the original ingredient appearances before hiding their drawables
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
			// interior width/depth, with clearance from the walls
			float fit = std::min(1.0f, std::min(1.04f / extent.x, 0.84f / extent.y));
			look.bin_scale = glm::vec3(fit, fit, 1.0f);
			look.grab_offset = glm::vec3(0.0f, 0.0f, mesh.max.z);
			prototype->pipeline.count = 0;
		}
	}

	for (Scene::Transform &transform : scene.transforms) {
		if (transform.name == "Plate") plate = &transform;
		if (transform.name == "Tray") tray = &transform;
		if (transform.name == "RecipeBoard") recipe_board = &transform;
	}
	if (!recipe_board) throw std::runtime_error("RecipeBoard not found.");
	// more readable orders without changing the source asset
	recipe_board->scale = glm::vec3(3.8f, 1.0f, 1.45f);
	recipe_board->position = glm::vec3(0.35f, 2.35f, 2.2f);
	std::array<char const *, 10> icon_names = {"BunBottom", "Patty", "Lettuce", "CheeseSlice", "BunTop",
		"TomatoSlice", "OnionRing", "PickleSlice", "BaconStrip", "SauceBlob"};
	glGenTextures(icon_textures.size(), icon_textures.data());
	for (size_t i = 0; i < icon_textures.size(); ++i) {
		glm::uvec2 size;
		std::vector<glm::u8vec4> pixels;
		load_png(data_path(std::string("icons/")+icon_names[i]+".png"), &size, &pixels, LowerLeftOrigin);
		glBindTexture(GL_TEXTURE_2D, icon_textures[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glGenVertexArrays(1, &icon_vao);
	glGenBuffers(1, &icon_buffer);
	glBindVertexArray(icon_vao);
	glBindBuffer(GL_ARRAY_BUFFER, icon_buffer);
	glVertexAttribPointer(lit_color_texture_program->Position_vec4, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), nullptr);
	glEnableVertexAttribArray(lit_color_texture_program->Position_vec4);
	glVertexAttribPointer(lit_color_texture_program->TexCoord_vec2, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), reinterpret_cast<void *>(3*sizeof(float)));
	glEnableVertexAttribArray(lit_color_texture_program->TexCoord_vec2);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	if (!plate) throw std::runtime_error("Plate not found.");
	plate_height = burger_meshes->lookup("Plate").max.z;
	if (!tray) throw std::runtime_error("Tray not found");
	tray_home = tray->position;
	scene.transforms.emplace_back();
	stack_root = &scene.transforms.back();
	stack_root->name = "BurgerStack";
	stack_root->parent = plate;

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
	{ // allocate visible bins plus one incoming row; no per-frame scene allocations
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
			stack_pool[i] = make_instance(stack_root, "Stack." + std::to_string(i));
		}
	}
	for (auto *bin : bin_transforms) bin_anchors.push_back(bin->position);
	bin_step = bin_anchors[1] - bin_anchors[0];
	bin_pipeline = bin_pool[0].drawable->pipeline;
	glm::mat4 local_from_world(bin_transforms[0]->parent->make_local_from_world());
	row_axis = glm::vec4(local_from_world[0][0], local_from_world[1][0],
		local_from_world[2][0], local_from_world[3][0]);
	row_bounds = glm::vec2(bin_anchors.front().x-bin_step.x*0.5f,
		bin_anchors.back().x+bin_step.x*0.5f);
	{ // reuse the counter mesh for fixed tunnel walls, with cuts hidden inside
		Mesh const &mesh = burger_meshes->lookup("Counter");
		Scene::Drawable::Pipeline shell_pipeline;
		for (auto const &drawable : scene.drawables) {
			if (drawable.transform->name == "Counter") shell_pipeline = drawable.pipeline;
		}
		if (!shell_pipeline.count) throw std::runtime_error("Missing counter mesh for conveyor housings");
		auto box = [&](std::string const &name, glm::vec3 center, glm::vec3 size) {
			Instance part = make_instance(bin_transforms[0]->parent, name);
			part.drawable->pipeline = shell_pipeline;
			part.transform->scale = size / (mesh.max-mesh.min);
			part.transform->position = center - (mesh.min+mesh.max)*0.5f*part.transform->scale;
		};
		float length = bin_step.x*0.85f;
		float y = bin_anchors.front().y, floor = bin_anchors.front().z;
		for (int side : {-1, 1}) {
			float edge = side < 0 ? row_bounds.x : row_bounds.y;
			float x = edge + float(side)*length*0.5f;
			std::string name = side < 0 ? "ConveyorExit" : "ConveyorEntry";
			box(name+".Roof", glm::vec3(x,y,floor+1.05f), glm::vec3(length,1.50f,0.16f));
			box(name+".Front", glm::vec3(x,y-0.69f,floor+0.50f), glm::vec3(length,0.12f,1.10f));
			box(name+".Back", glm::vec3(x,y+0.69f,floor+0.50f), glm::vec3(length,0.12f,1.10f));
			box(name+".Base", glm::vec3(x,y,floor-0.06f), glm::vec3(length,1.50f,0.12f));
		}
		row_bounds += glm::vec2(-length*0.5f,length*0.5f);
	}
	slide_starts.resize(bin_pool.size());
	clear_stack();
	sync_supplies();
	std::cout << "Burger seed: " << game.seed << std::endl;
}

PlayMode::~PlayMode() {
	glDeleteTextures(icon_textures.size(), icon_textures.data());
	glDeleteBuffers(1, &icon_buffer);
	glDeleteVertexArrays(1, &icon_vao);
}

void PlayMode::draw_order_icons(glm::mat4 const &clip_from_board) {
	auto const &program = *lit_color_texture_program;
	glUseProgram(program.program);
	glUniformMatrix4fv(program.CLIP_FROM_OBJECT_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_board));
	glUniformMatrix4x3fv(program.LIGHT_FROM_OBJECT_mat4x3, 1, GL_FALSE, glm::value_ptr(glm::mat4x3(1)));
	glUniformMatrix3fv(program.LIGHT_FROM_NORMAL_mat3, 1, GL_FALSE, glm::value_ptr(glm::mat3(1)));
	glUniform1i(program.ROW_CLIP_int, 0);
	glUniform1i(program.LIGHT_TYPE_int, 1);
	glUniform3f(program.LIGHT_DIRECTION_vec3, 0, 0, 0);
	glUniform3f(program.LIGHT_ENERGY_vec3, 2, 2, 2);
	glBindVertexArray(icon_vao);
	glVertexAttrib4f(program.Color_vec4, 1, 1, 1, 1);
	glVertexAttrib3f(program.Normal_vec3, 0, 0, 1);
	glBindBuffer(GL_ARRAY_BUFFER, icon_buffer);
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	for (size_t card = 0; card < 2; ++card) {
		auto const &order = card == 0 ? game.order : game.waiting_order;
	for (size_t i = 0; i < order.layers.size(); ++i) {
		float x = -1.10f + float(card)*1.22f + float(i%5)*0.215f;
		float z = i < 5 ? 1.20f : 0.65f;
		float w = 0.17f, h = 0.43f, y = -0.12f;
		float vertices[] = {x,y,z,0,0, x+w,y,z,1,0, x+w,y,z+h,1,1,
			x,y,z,0,0, x+w,y,z+h,1,1, x,y,z+h,0,1};
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
		glBindTexture(GL_TEXTURE_2D, icon_textures.at(static_cast<size_t>(order.layers[i])));
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

void PlayMode::clip_row() {
	glUniform1i(lit_color_texture_program->ROW_CLIP_int, 1);
	glUniform4fv(lit_color_texture_program->ROW_AXIS_vec4, 1, glm::value_ptr(row_axis));
	glUniform2fv(lit_color_texture_program->ROW_BOUNDS_vec2, 1, glm::value_ptr(row_bounds));
}

void PlayMode::set_supply(size_t index, burger::Ingredient ingredient) {
	Instance &instance = supply_pool.at(index);
	IngredientLook const &look = ingredient_looks.at(static_cast<size_t>(ingredient));
	instance.drawable->pipeline = look.pipeline;
	instance.drawable->pipeline.set_uniforms = [this]() { clip_row(); };
	instance.transform->parent = bin_pool[index].transform;
	instance.transform->rotation = glm::quat(1,0,0,0);
	instance.transform->scale = look.bin_scale;
	glm::vec3 center = 0.5f * (look.min + look.max);
	instance.transform->position = glm::vec3(-center.x * look.bin_scale.x,
		-center.y * look.bin_scale.y, 0.12f - look.min.z * look.bin_scale.z);
}

void PlayMode::sync_supplies() {
	assert(game.bins.size() == bin_transforms.size());
	for (size_t i = 0; i < bin_pool.size(); ++i) {
		bin_pool[i].drawable->pipeline = bin_pipeline;
		bin_pool[i].drawable->pipeline.set_uniforms = [this]() { clip_row(); };
		supply_pool[i].transform->parent = bin_pool[i].transform;
		if (i >= game.bins.size()) {
			bin_pool[i].drawable->pipeline.count = 0;
			supply_pool[i].drawable->pipeline.count = 0;
			continue;
		}
		bin_pool[i].transform->position = bin_anchors[i];
		bin_transforms[i] = bin_pool[i].transform;
		set_supply(i, game.bins[i]);
	}
}

void PlayMode::begin_slide() {
	assert(game.pending);
	size_t n = game.bins.size(), k = game.pending->supply.removed;
	// incoming food is finalized while still outside the visible row
	for (size_t j = 0; j < k; ++j) {
		size_t i = n+j;
		bin_pool[i].transform->position = bin_anchors.back() + float(j+1)*bin_step;
		bin_pool[i].drawable->pipeline = bin_pipeline;
		bin_pool[i].drawable->pipeline.set_uniforms = [this]() { clip_row(); };
		set_supply(i, game.pending->supply.after[n-k+j]);
	}
	for (size_t i = 0; i < n+k; ++i) slide_starts[i] = bin_pool[i].transform->position;
	phase = Phase::Sliding;
	move_time = 0.0f;
	move_duration = 0.35f + 0.20f*float(k);
}

void PlayMode::finish_slide() {
	size_t n = game.bins.size(), k = game.pending->supply.removed;
	// preserve each surviving bin and its food instead of repainting slots
	std::rotate(bin_pool.begin(), bin_pool.begin()+k, bin_pool.begin()+n+k);
	std::rotate(supply_pool.begin(), supply_pool.begin()+k, supply_pool.begin()+n+k);
	for (size_t i = 0; i < bin_pool.size(); ++i) {
		if (i < n) {
			bin_pool[i].transform->position = bin_anchors[i];
			bin_transforms[i] = bin_pool[i].transform;
		} else {
			bin_pool[i].drawable->pipeline.count = 0;
			supply_pool[i].drawable->pipeline.count = 0;
			supply_pool[i].transform->parent = bin_pool[i].transform;
		}
	}
	if (game.pending->outcome == burger::PickOutcome::Completed) clear_stack();
	bool committed = game.commit_advance();
	assert(committed);
	(void)committed;
	selected_slot = -1;
	released = false;
	phase = Phase::Ready;
}

void PlayMode::clear_stack() {
	for (Instance &instance : stack_pool) instance.drawable->pipeline.count = 0;
	stack_count = 0;
	stack_height = plate_height;
	stack_root->position = glm::vec3(0);
}

void PlayMode::begin_serve() {
	// the tray floor is 0.08 above its local origin, below the raised side rails
	glm::vec3 floor = tray->make_world_from_local() * glm::vec4(0,0,0.08f,1);
	serve_target = glm::vec3(plate->make_local_from_world() * glm::vec4(floor,1)) -
		glm::vec3(0,0,plate_height);
	dispatch_offset = glm::vec3(18.0f,0,0);
	move_arm(travel_center, Phase::Serving, 0.80f);
}

void PlayMode::restart_game() {
	game.reset_run(std::random_device{}());
	phase = Phase::Ready;
	selected_slot = -1;
	carrying = false;
	released = false;
	move_time = 0.0f;
	apply_arm(angles_for(travel_center));
	set_fingers(0.0f);
	clear_stack();
	tray->position = tray_home;
	sync_supplies();
	std::cout << "Burger seed: " << game.seed << std::endl;
}

glm::vec3 PlayMode::tool_position() const {
	return gripper->make_world_from_local() * glm::vec4(tool_local, 1.0f);
}

glm::vec3 PlayMode::angles_for(glm::vec3 const &target) const {
	// solve only this two-link arm, with the wrist kept level
	glm::vec3 wrist_offset = gripper->position + gripper->scale * tool_local;
	glm::vec3 local = arm_yaw->parent->make_local_from_world() * glm::vec4(target - wrist_offset, 1.0f);
	local -= arm_yaw->position + arm_shoulder->position;
	float radius = glm::length(glm::vec2(local));
	float distance = glm::length(local);
	if (distance >= upper_length + lower_length || distance <= std::abs(upper_length - lower_length)) {
		throw std::runtime_error("Arm target outside reach: " + std::to_string(distance));
	}
	float elbow = std::acos(std::clamp((distance*distance - upper_length*upper_length -
		lower_length*lower_length) / (2.0f*upper_length*lower_length), -1.0f, 1.0f));
	float shoulder = std::atan2(radius, local.z) -
		std::atan2(lower_length*std::sin(elbow), upper_length + lower_length*std::cos(elbow));
	return glm::vec3(std::atan2(local.y, local.x), shoulder, elbow);
}

void PlayMode::apply_arm(glm::vec3 const &angles) {
	arm_angles = angles;
	arm_yaw->rotation = glm::angleAxis(angles.x, glm::vec3(0,0,1));
	arm_shoulder->rotation = glm::angleAxis(angles.y - upper_rest_angle, glm::vec3(0,1,0));
	arm_forearm->rotation = glm::angleAxis(angles.z, glm::vec3(0,1,0));
	arm_wrist->rotation = glm::angleAxis(-angles.y-angles.z, glm::vec3(0,1,0)) *
		glm::angleAxis(-angles.x, glm::vec3(0,0,1));
}

void PlayMode::set_fingers(float closed) {
	finger_left->position = finger_left_home + glm::vec3(-0.06f*closed,0,0);
	finger_right->position = finger_right_home + glm::vec3(0.06f*closed,0,0);
}

void PlayMode::move_arm(glm::vec3 const &target, Phase next_phase, float duration) {
	move_from = arm_angles;
	move_to = angles_for(target);
	move_to.x = move_from.x + std::remainder(move_to.x - move_from.x, 2.0f*float(M_PI));
	move_duration = duration;
	move_time = 0.0f;
	phase = next_phase;
}

glm::vec3 PlayMode::supply_target(size_t slot) const {
	IngredientLook const &look = ingredient_looks.at(static_cast<size_t>(game.bins.at(slot)));
	glm::vec3 center = (look.min + look.max)*0.5f;
	return supply_pool.at(slot).transform->make_world_from_local() *
		glm::vec4(center.x, center.y, look.max.z + 0.08f, 1.0f);
}

void PlayMode::select_slot(size_t slot) {
	selected_slot = static_cast<int>(slot);
	pickup = supply_target(slot);
	glm::vec3 hover = pickup;
	hover.z = 2.7f;
	move_arm(hover, Phase::Selecting, 0.40f);
}

void PlayMode::pick_selected() {
	if (phase != Phase::Hovering || selected_slot < 0) return;
	if (!game.begin_pick(static_cast<size_t>(selected_slot))) return;
	released = false;
	pickup = supply_target(static_cast<size_t>(selected_slot));
	IngredientLook const &look = ingredient_looks.at(static_cast<size_t>(game.pending->ingredient));
	float thickness = (look.max.z-look.min.z)*look.stack_scale.z;
	if (game.pending->outcome == burger::PickOutcome::Wrong) {
		destination = trash->make_world_from_local() * glm::vec4(0,0,1.35f+thickness+0.08f,1);
	} else {
		destination = plate->make_world_from_local() * glm::vec4(0,0,stack_height+thickness+0.08f,1);
	}
	move_arm(pickup, Phase::Descending, 0.30f);
}

void PlayMode::release_ingredient() {
	assert(game.pending && carrying);
	Instance &source = supply_pool.at(game.pending->slot);
	if (game.pending->outcome == burger::PickOutcome::Wrong) {
		clear_stack();
	} else {
		assert(stack_count < stack_pool.size());
		IngredientLook const &look = ingredient_looks.at(static_cast<size_t>(game.pending->ingredient));
		Instance &instance = stack_pool[stack_count++];
		instance.drawable->pipeline = look.pipeline;
		instance.transform->scale = look.stack_scale;
		glm::vec3 center = 0.5f*(look.min+look.max);
		instance.transform->position = glm::vec3(-center.x*look.stack_scale.x,
			-center.y*look.stack_scale.y, stack_height-look.min.z*look.stack_scale.z);
		stack_height += (look.max.z-look.min.z)*look.stack_scale.z;
	}
	source.drawable->pipeline.count = 0;
	carrying = false;
	released = true;
	game.settle_pick(static_cast<uint32_t>(game.order.layers.size() * 2));
}

void PlayMode::finish_phase() {
	switch (phase) {
	case Phase::Selecting:
		phase = Phase::Hovering;
		return;
	case Phase::Descending:
		phase = Phase::Closing;
		move_time = 0.0f;
		move_duration = 0.12f;
		return;
	case Phase::Closing: {
		// keep the food's world transform when attaching it to the palm
		Instance &source = supply_pool.at(game.pending->slot);
		glm::mat4 local = glm::mat4(gripper->make_local_from_world()) * glm::mat4(source.transform->make_world_from_local());
		source.transform->position = glm::vec3(local[3]);
		source.transform->scale = glm::vec3(glm::length(glm::vec3(local[0])),
			glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
		source.transform->rotation = glm::quat_cast(glm::mat3(
			glm::vec3(local[0])/source.transform->scale.x,
			glm::vec3(local[1])/source.transform->scale.y,
			glm::vec3(local[2])/source.transform->scale.z));
		source.transform->parent = gripper;
		source.drawable->pipeline.set_uniforms = ingredient_looks.at(static_cast<size_t>(game.pending->ingredient)).pipeline.set_uniforms;
		carrying = true;
		glm::vec3 hover = pickup;
		hover.z = 2.7f;
		move_arm(hover, Phase::Lifting, 0.28f);
		return;
	}
	case Phase::Lifting:
		move_arm(travel_center, Phase::Transporting, 0.34f);
		return;
	case Phase::Transporting: {
		glm::vec3 hover = destination + glm::vec3(0,0,0.55f);
		move_arm(hover, Phase::Approaching, 0.34f);
		return;
	}
	case Phase::Approaching:
		move_arm(destination, Phase::Placing, 0.32f);
		return;
	case Phase::Placing:
		release_ingredient();
		if (game.game_over()) {
			phase = Phase::GameOver;
			return;
		}
		phase = Phase::Opening;
		move_time = 0.0f;
		move_duration = 0.12f;
		return;
	case Phase::Opening:
		move_arm(destination + glm::vec3(0,0,0.55f), Phase::Retreating, 0.23f);
		return;
	case Phase::Retreating:
		phase = Phase::Feedback;
		move_time = 0.0f;
		move_duration = game.pending->outcome == burger::PickOutcome::Completed ? 0.35f : 0.08f;
		return;
	case Phase::Feedback:
		if (game.pending->outcome == burger::PickOutcome::Completed) begin_serve();
		else begin_slide();
		return;
	case Phase::Serving:
		phase = Phase::Dispatching;
		move_time = 0;
		move_duration = 0.85f;
		return;
	case Phase::Dispatching:
		clear_stack();
		phase = Phase::ReturningTray;
		move_time = 0;
		move_duration = 0.65f;
		return;
	case Phase::ReturningTray:
		tray->position = tray_home;
		begin_slide();
		return;
	case Phase::Sliding:
		finish_slide();
		return;
	default: return;
	}
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
	if (phase != Phase::Ready && phase != Phase::Hovering) return true;
	if (evt.key.key == SDLK_TAB) {
		if (game.switch_order()) {
			selected_slot = -1;
			phase = Phase::Ready;
		}
		return true;
	}
	if (evt.key.key >= SDLK_1 && evt.key.key <= SDLK_6) {
		select_slot(static_cast<size_t>(evt.key.key - SDLK_1));
		return true;
	}
	if (evt.key.key == SDLK_RETURN || evt.key.key == SDLK_KP_ENTER) {
		pick_selected();
		return true;
	}
	return false;
}

void PlayMode::update(float elapsed) {
	// consume leftover time so animation timing does not depend on frame rate
	while (elapsed > 0.0f && phase != Phase::GameOver) {
		if (phase == Phase::Ready || phase == Phase::Hovering) {
			game.advance_time(elapsed);
			if (game.game_over()) phase = Phase::GameOver;
			return;
		}
		float step = std::min(elapsed, move_duration-move_time);
		// expiry wins ties with a release or conveyor commit
		step = std::min(step, game.time_left);
		game.advance_time(step);
		move_time += step;
		elapsed -= step;
		if (game.game_over()) {
			phase = Phase::GameOver;
			return;
		}
		float t = std::clamp(move_time/move_duration, 0.0f, 1.0f);
		float smooth = t*t*(3.0f-2.0f*t);
		if (phase == Phase::Serving) {
			stack_root->position = serve_target*smooth + glm::vec3(0,0,0.55f*std::sin(float(M_PI)*t));
			apply_arm(glm::mix(move_from, move_to, smooth));
		} else if (phase == Phase::Dispatching || phase == Phase::ReturningTray) {
			float amount = phase == Phase::Dispatching ? smooth : 1.0f-smooth;
			glm::vec3 world_offset = dispatch_offset*amount;
			tray->position = tray_home + glm::vec3(tray->parent->make_local_from_world()*glm::vec4(world_offset,0));
			if (phase == Phase::Dispatching) {
				stack_root->position = serve_target +
					glm::vec3(plate->make_local_from_world()*glm::vec4(world_offset,0));
			}
		} else if (phase == Phase::Sliding) {
			size_t k = game.pending->supply.removed;
			for (size_t i = 0; i < game.bins.size()+k; ++i) {
				bin_pool[i].transform->position = slide_starts[i] - float(k)*bin_step*smooth;
			}
		}
		else if (phase == Phase::Closing) set_fingers(smooth);
		else if (phase == Phase::Opening) set_fingers(1.0f-smooth);
		else if (phase != Phase::Feedback) apply_arm(glm::mix(move_from, move_to, smooth));
		if (phase == Phase::Transporting && carrying) {
			IngredientLook const &look = ingredient_looks.at(static_cast<size_t>(game.pending->ingredient));
			Scene::Transform &food = *supply_pool[game.pending->slot].transform;
			food.scale = glm::mix(look.bin_scale, look.stack_scale, smooth) / gripper->scale;
			glm::vec3 center = (look.min+look.max)*0.5f;
			food.position.x = -center.x*food.scale.x;
			food.position.y = -center.y*food.scale.y;
		}
		if (move_time >= move_duration) finish_phase();
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);
	// preserve the counter and board width in narrower windows
	camera->fovy = 2.0f*std::atan(std::tan(camera_fovy*0.5f) *
		std::max(1.0f, (16.0f/9.0f)/camera->aspect));

	//set up light type and position for lit_color_texture_programs
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.042f, 0.066f, 0.080f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it

	GL_ERRORS(); //print any errors produced by this setup code

	scene.draw(*camera);



	{ // order text lies on the green panel, in RecipeBoard local coordinates
		glm::mat4 clip_from_world = camera->make_projection() * glm::mat4(camera->transform->make_local_from_world());
		glm::mat4 clip_from_board = clip_from_world * glm::mat4(recipe_board->make_world_from_local());
		draw_order_icons(clip_from_board);
		DrawLines lines(clip_from_board);
		auto text = [&](std::string const &label, float x, float z, float height, glm::u8vec4 color) {
			lines.draw_text(label, glm::vec3(x,-0.116f,z), glm::vec3(height,0,0),
				glm::vec3(0,0,height), color);
		};
		glm::u8vec4 ink(24,48,44,255);
		lines.draw(glm::vec3(0.055f,-0.125f,0.55f), glm::vec3(0.055f,-0.125f,1.86f), ink);
		text("TIME " + std::to_string(static_cast<int>(std::ceil(game.time_left))) +
			" / SCORE " + std::to_string(game.score), -0.25f, 1.89f, 0.075f, ink);
		for (size_t card = 0; card < 2; ++card) {
			auto const &order = card == 0 ? game.order : game.waiting_order;
			for (size_t i = 0; i < order.layers.size(); ++i) {
				float x = -1.10f + float(card)*1.22f + float(i%5)*0.215f;
				float z = i < 5 ? 1.20f : 0.65f;
				if (i+1 < order.layers.size() && i%5 != 4) text("+",x+0.183f,z+0.20f,0.065f,ink);
			}
		}
		std::string status;
		if (released && game.pending) {
			status = game.pending->outcome == burger::PickOutcome::Wrong ? "WRONG - TRY AGAIN" :
				(game.pending->outcome == burger::PickOutcome::Completed ? "ORDER COMPLETE!" : "CORRECT");
		}
		if (phase == Phase::Hovering) status = "ENTER TO GRAB";
		if (phase == Phase::GameOver) status = "TIME UP - R TO RESTART";
		text(status, -1.07f, 0.52f, 0.08f, ink);
	}
	{ // right-aligned controls stay outside the order board
		glDisable(GL_DEPTH_TEST);
		float aspect = camera->aspect;
		float height = std::min(0.04f, aspect / 20.0f);
		DrawLines lines(glm::mat4(1.0f/aspect,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1));
		std::array<std::string, 3> hints = {"1-6 SELECT / ENTER PICK",
			game.order_locked ? "ORDER LOCKED" : "TAB SWITCH ORDER", "R RESTART / ESC QUIT"};
		for (size_t i = 0; i < hints.size(); ++i) {
			size_t first = lines.attribs.size();
			glm::vec3 end;
			lines.draw_text(hints[i], glm::vec3(0,-0.80f-float(i)*0.07f,0),
				glm::vec3(height,0,0), glm::vec3(0,height,0), glm::u8vec4(245,245,230,255), &end);
			for (size_t v = first; v < lines.attribs.size(); ++v) {
				lines.attribs[v].Position.x += aspect - 0.06f - end.x;
			}
		}
	}
	GL_ERRORS();
}
