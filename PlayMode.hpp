#include "Mode.hpp"

#include "Scene.hpp"
#include "BurgerLogic.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <array>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	enum class Phase { Ready, Feedback };
	Phase phase = Phase::Ready;
	int selected_slot = -1;
	float feedback_time = 0.0f;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;
	std::vector<Scene::Transform *> bin_transforms;
	burger::BurgerLogic game;
	struct IngredientLook {
		char const *name = nullptr;
		Scene::Drawable::Pipeline pipeline;
		glm::vec3 min = glm::vec3(0.0f), max = glm::vec3(0.0f);
		glm::vec3 bin_scale = glm::vec3(1.0f);
		glm::vec3 stack_scale = glm::vec3(1.0f);
		glm::vec3 grab_offset = glm::vec3(0.0f);
	};
	std::array<IngredientLook, 10> ingredient_looks;
	struct Instance {
		Scene::Transform *transform = nullptr;
		Scene::Drawable *drawable = nullptr;
	};
	std::vector<Instance> bin_pool, supply_pool;
	std::array<Instance, burger::MaxLayers> stack_pool;
	size_t stack_count = 0;
	float stack_height = 0.0f;
	float plate_height = 0.0f;
	Scene::Transform *plate = nullptr;
	Scene::Transform *recipe_board = nullptr;

	void sync_supplies();
	void clear_stack();
	void restart_game();
	void pick_selected(); // Immediate placement until Step 3 supplies animation.

	//burger-bot arm joints to animate:
	Scene::Transform *arm_yaw = nullptr;
	Scene::Transform *arm_shoulder = nullptr;
	Scene::Transform *arm_forearm = nullptr;
	glm::quat arm_yaw_base_rotation;
	glm::quat arm_shoulder_base_rotation;
	glm::quat arm_forearm_base_rotation;
	
	//camera:
	Scene::Camera *camera = nullptr;

};
