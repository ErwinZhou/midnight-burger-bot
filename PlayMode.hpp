#include "Mode.hpp"

#include "Scene.hpp"
#include "BurgerLogic.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;
	std::vector<Scene::Transform *> bin_transforms;
	burger::BurgerLogic game;

	//burger-bot arm joints to animate:
	Scene::Transform *arm_yaw = nullptr;
	Scene::Transform *arm_shoulder = nullptr;
	Scene::Transform *arm_forearm = nullptr;
	glm::quat arm_yaw_base_rotation;
	glm::quat arm_shoulder_base_rotation;
	glm::quat arm_forearm_base_rotation;
	float wobble = 0.0f;
	
	//camera:
	Scene::Camera *camera = nullptr;

};
