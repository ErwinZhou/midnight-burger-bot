#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace burger {

enum class Ingredient : uint8_t {
	BunBottom, Patty, Lettuce, Cheese, BunTop, Tomato, Onion, Pickle, Bacon, Sauce
};
constexpr size_t BinCount = 6;
constexpr size_t MinLayers = 3;
constexpr size_t MaxLayers = 10;
using Bins = std::array<Ingredient, BinCount>;

struct Order {
	std::vector<Ingredient> layers;
	size_t next = 0;
	bool operator==(Order const &) const = default;
};

struct SupplyPlan {
	size_t removed = 0;
	Bins after{}; // survivors first: new supplies occupy [6 - removed, 6)
	bool operator==(SupplyPlan const &) const = default;
};

Order generate_order(std::mt19937 &rng);
// k is a count (1..6) but keyboard/scene slot indices are zero-based
SupplyPlan plan_advance(Bins const &before, size_t k, Ingredient needed, std::mt19937 &rng);

enum class PickOutcome { Correct, Wrong, Completed };
struct PendingPick {
	size_t slot = 0;
	Ingredient ingredient = Ingredient::BunBottom;
	PickOutcome outcome = PickOutcome::Correct;
	Order next_order;
	SupplyPlan supply;
	bool operator==(PendingPick const &) const = default;
};

struct BurgerLogic {
	uint32_t seed = 0;
	std::mt19937 rng;
	Order order;
	Bins bins{};
	std::optional<PendingPick> pending;

	// Keep seed available for replay
	explicit BurgerLogic(uint32_t initial_seed = std::random_device{}());
	void reset_run(uint32_t new_seed);
	bool begin_pick(size_t slot);
	bool commit_advance();

	Ingredient next_needed() const {
		assert(order.next < order.layers.size() && "Active order must have a next ingredient.");
		return order.layers.at(order.next);
	}
	bool next_available() const;
};

} // namespace burger
