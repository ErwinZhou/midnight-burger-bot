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

	enum class Phase { Ready, Selecting, Hovering, Descending, Closing, Lifting,
		Transporting, Approaching, Placing, Opening, Retreating, Feedback, Sliding, GameOver };
	Phase phase = Phase::Ready;
	int selected_slot = -1;

	//local copy of the game scene:
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
	std::array<GLuint, 10> icon_textures{};
	GLuint icon_vao = 0, icon_buffer = 0;
	void draw_order_icons(glm::mat4 const &clip_from_board);

	std::vector<glm::vec3> bin_anchors, slide_starts;
	Scene::Drawable::Pipeline bin_pipeline;
	glm::vec3 bin_step{};
	glm::vec4 row_axis{};
	glm::vec2 row_bounds{};
	void clip_row();
	void set_supply(size_t index, burger::Ingredient ingredient);
	void begin_slide();
	void finish_slide();
	void sync_supplies();
	void clear_stack();
	void restart_game();
	void pick_selected();
	void select_slot(size_t slot);
	void move_arm(glm::vec3 const &target, Phase next_phase, float duration);
	glm::vec3 angles_for(glm::vec3 const &target) const;
	void apply_arm(glm::vec3 const &angles);
	glm::vec3 tool_position() const;
	glm::vec3 supply_target(size_t slot) const;
	void release_ingredient();
	void finish_phase();
	void set_fingers(float closed);
	Scene::Transform *arm_wrist = nullptr;
	Scene::Transform *gripper = nullptr;
	Scene::Transform *finger_left = nullptr;
	Scene::Transform *finger_right = nullptr;
	Scene::Transform *trash = nullptr;
	glm::vec3 finger_left_home{}, finger_right_home{};
	glm::vec3 arm_angles{}, move_from{}, move_to{};
	glm::vec3 pickup{}, destination{}, travel_center{};
	float move_time = 0.0f, move_duration = 1.0f;
	float upper_length = 0.0f, lower_length = 0.0f;
	float upper_rest_angle = 0.0f;
	bool carrying = false;
	bool released = false;
	glm::vec3 const tool_local = glm::vec3(0.0f, 0.0f, -0.40f);

	//burger-bot arm joints to animate
	Scene::Transform *arm_yaw = nullptr;
	Scene::Transform *arm_shoulder = nullptr;
	Scene::Transform *arm_forearm = nullptr;
	
	//camera
	Scene::Camera *camera = nullptr;

};
