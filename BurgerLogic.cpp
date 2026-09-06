#include "BurgerLogic.hpp"

#include <algorithm>
#include <stdexcept>

namespace burger {
namespace {

constexpr std::array<Ingredient, 10> Ingredients = {
	Ingredient::BunBottom, Ingredient::Patty, Ingredient::Lettuce,
	Ingredient::Cheese, Ingredient::BunTop, Ingredient::Tomato,
	Ingredient::Onion, Ingredient::Pickle, Ingredient::Bacon, Ingredient::Sauce
};
constexpr std::array<Ingredient, 8> Fillings = {
	Ingredient::Patty, Ingredient::Lettuce, Ingredient::Cheese, Ingredient::Tomato,
	Ingredient::Onion, Ingredient::Pickle, Ingredient::Bacon, Ingredient::Sauce
};

size_t choose(std::mt19937 &rng, size_t low, size_t high) {
	return std::uniform_int_distribution<size_t>(low, high)(rng);
}

} // namespace

Order generate_order(std::mt19937 &rng) {
	Order order;
	size_t count = choose(rng, MinLayers, MaxLayers);
	order.layers.reserve(count);
	order.layers.push_back(Ingredient::BunBottom);
	for (size_t i = 1; i + 1 < count; ++i) {
		order.layers.push_back(Fillings[choose(rng, 0, Fillings.size() - 1)]);
	}
	order.layers.push_back(Ingredient::BunTop);
	return order;
}

SupplyPlan plan_advance(Bins const &before, size_t k, Ingredient needed, std::mt19937 &rng) {
	if (k == 0 || k > BinCount) throw std::out_of_range("Supply advance must remove 1..6 bins.");
	if (std::find(Ingredients.begin(), Ingredients.end(), needed) == Ingredients.end()) {
		throw std::invalid_argument("Unknown required ingredient.");
	}
	SupplyPlan plan;
	plan.removed = k;
	size_t survivors = BinCount - k;
	bool available = false;
	for (size_t i = 0; i < survivors; ++i) {
		plan.after[i] = before[i + k];
		available = available || plan.after[i] == needed;
	}
	// Decide the guaranteed slot before filling any new bin. Never change survivors
	size_t guaranteed = available ? BinCount : choose(rng, survivors, BinCount - 1);
	for (size_t i = survivors; i < BinCount; ++i) {
		plan.after[i] = i == guaranteed ? needed : Ingredients[choose(rng, 0, Ingredients.size() - 1)];
	}
	return plan;
}

BurgerLogic::BurgerLogic(uint32_t initial_seed) {
	reset_run(initial_seed);
}

void BurgerLogic::reset_run(uint32_t new_seed) {
	seed = new_seed;
	rng.seed(seed);
	pending.reset();
	order = generate_order(rng);
	bins = plan_advance(Bins{}, BinCount, next_needed(), rng).after;
	assert(next_available() && "Initial supplies must contain the next ingredient.");
}

bool BurgerLogic::begin_pick(size_t slot) {
	if (slot >= BinCount) throw std::out_of_range("Pick slot must be 0..5.");
	if (pending) return false;
	PendingPick pick;
	pick.slot = slot;
	pick.ingredient = bins[slot];
	pick.next_order = order;
	if (pick.ingredient != next_needed()) {
		pick.outcome = PickOutcome::Wrong;
		pick.next_order.next = 0;
	} else {
		++pick.next_order.next;
		if (pick.next_order.next == pick.next_order.layers.size()) {
			pick.outcome = PickOutcome::Completed;
			pick.next_order = generate_order(rng);
		}
	}
	pick.supply = plan_advance(bins, slot + 1,
		pick.next_order.layers.at(pick.next_order.next), rng);
	pending = std::move(pick);
	return true;
}

bool BurgerLogic::commit_advance() {
	if (!pending) return false;
	bins = pending->supply.after;
	order = std::move(pending->next_order);
	pending.reset();
	assert(next_available() && "Committed supplies must contain the next ingredient.");
	return true;
}

bool BurgerLogic::next_available() const {
	return std::find(bins.begin(), bins.end(), next_needed()) != bins.end();
}

} // namespace burger
