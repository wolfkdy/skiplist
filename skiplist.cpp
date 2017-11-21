#include <iostream>
#include <cstdlib>
#include <list>
#include <assert.h>
#include <random>
#include <chrono>
#include "skiplist.h"

namespace Concurrent {
SkipListNode::SkipListNode(uint64_t key, const std::string& value, uint8_t level)
	:key(key),
	 value(value),
     forward(level+1),
	 marked(false),
	 fullyLinked(false) {
#ifdef DEBUG
	alive.fetch_add(1);
#endif
}

SkipListNode::~SkipListNode() {
#ifdef DEBUG
	alive.fetch_sub(1);
#endif
};

uint8_t SkipListNode::top_level() {
	return forward.size() - 1;
}

std::atomic<size_t> SkipListNode::alive(0);

std::unique_ptr<SkipListNode> SkipList::makeNode(uint8_t lvl,
		uint64_t key, const std::string& value) {
	return std::unique_ptr<SkipListNode>(new SkipListNode(
			key,
			value,
			lvl));
}

SkipList::SkipList(uint8_t max_level)
	:_max_level(max_level),
	 _level(1) {
	// key,value for headnode is meanless
	_head = std::move(SkipList::makeNode(max_level, 0, ""));
	for (size_t i = 0; i <= max_level; i++) {
		_head->forward[i] = SkipList::makeNode(0, std::numeric_limits<uint64_t>::max(), "");
	}
}

uint8_t SkipList::randomLevel() const {
	static thread_local std::mt19937 generator(std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> distribution(0,1);
	uint8_t lvl = 1;
	while(distribution(generator) && lvl < _max_level) {
		++lvl;
	}
	return lvl;
}

void SkipList::traverse() {
	for (size_t i = _max_level; i >= 1; i--) {
		SkipListNode *node = _head.get();
		std::cout<< "level:" << i << ":";
		while(node->forward[i]->key != std::numeric_limits<uint64_t>::max()) {
			std::cout<< node->forward[i]->key<< ",";
			node = node->forward[i].get();
		}
		std::cout << std::endl;
	}
}
bool SkipList::findNode(uint64_t key, std::vector<std::shared_ptr<SkipListNode>>* preds,
		std::vector<std::shared_ptr<SkipListNode>>* succs, uint8_t* layer) {
	std::shared_ptr<SkipListNode> prev = _head;
	bool found = false;
	assert(preds->size() >= _max_level+1);
	assert(succs->size() >= _max_level+1);
	for (size_t i = _max_level; i >= 1; --i) {
		std::shared_ptr<SkipListNode> curr = prev->forward[i];
		while (curr->key < key) {
			prev = curr;
			curr = curr->forward[i];
		}
		if (!found && curr->key == key) {
			*layer = i;
			found = true;
		}
		(*preds)[i] = prev;
		(*succs)[i] = curr;
	}
	return found;
}

bool SkipList::contains(uint64_t key) {
	SkipListNode *x = _head.get();
	for (size_t i = _level; i >= 1; --i) {
		while (x->forward[i]->key < key) {
			x = x->forward[i].get();
		}
	}
	x = x->forward[1].get();
	if (x->key == key) {
		return true;
	}
	return false;
}

bool SkipList::concurrentInsert(uint64_t key, const std::string& value) {
	uint8_t top_layer = SkipList::randomLevel();
	std::vector<std::shared_ptr<SkipListNode>> preds(_max_level+1);
	std::vector<std::shared_ptr<SkipListNode>> succs(_max_level+1);
	while(true) {
		uint8_t found_level = 0;
		bool found = findNode(key, &preds, &succs, &found_level);
		if (found) {
			SkipListNode* node_found = succs[found_level].get();
			if (!node_found->marked.load()) {
				while(!node_found->fullyLinked.load()) {;}
				return false;
			} else {
				continue;
			}
		}
		{
			// a list of unique_ptr wrapped lockgurad
			std::list<std::unique_ptr<std::lock_guard<std::mutex>>> lock_guards;

			SkipListNode *pred = nullptr, *succ = nullptr, *prevPred = nullptr;
			bool valid = true;
			for (uint8_t layer = 1; valid && layer <= top_layer; ++layer) {
				pred = preds[layer].get();
				succ = succs[layer].get();
				if (pred != prevPred) {
					lock_guards.push_back(
						std::unique_ptr<std::lock_guard<std::mutex>>(
							new std::lock_guard<std::mutex>(
								pred->mutex
							)
						)
					);
					prevPred = pred;
				}
				valid = !pred->marked.load() && !succ->marked.load() && pred->forward[layer].get() == succ;
			}
			if (!valid) {
				continue;
			}
			auto p = SkipList::makeNode(top_layer, key, value);
			std::shared_ptr<SkipListNode> sp = std::move(p);
			for (uint8_t layer = 1; layer <= top_layer; ++layer) {
				sp->forward[layer] = succs[layer];
				preds[layer]->forward[layer] = sp;
			}
			sp->fullyLinked.store(true);
			return true;
		}
	}
}

bool SkipList::okToDelete(SkipListNode *node, uint8_t found_level) {
	return (node->fullyLinked.load() && node->top_level() == found_level && (!node->marked.load()));
}

bool SkipList::concurrentContains(uint64_t key) {
	std::vector<std::shared_ptr<SkipListNode>> preds(_max_level+1);
	std::vector<std::shared_ptr<SkipListNode>> succs(_max_level+1);
	uint8_t found_level = 0;
	bool found = findNode(key, &preds, &succs, &found_level);
	return (found && succs[found_level]->fullyLinked.load() && !succs[found_level]->marked.load());
}

bool SkipList::concurrentErase(uint64_t key) {
	std::vector<std::shared_ptr<SkipListNode>> preds(_max_level+1);
	std::vector<std::shared_ptr<SkipListNode>> succs(_max_level+1);
	std::shared_ptr<SkipListNode> node_to_delete;
	bool is_marked = false;
	int32_t top_layer = -1;
	while(true) {
		uint8_t found_level = 0;
		bool found = findNode(key, &preds, &succs, &found_level);
		if (is_marked || (found && okToDelete(succs[found_level].get(), found_level))) {
			if (!is_marked) {
				node_to_delete = succs[found_level];
				top_layer = node_to_delete->top_level();
				node_to_delete->mutex.lock();
				if (node_to_delete->marked.load()) {
					node_to_delete->mutex.unlock();
					return false;
				}
				node_to_delete->marked.store(true);
				is_marked = true;
			}
			{
				std::list<std::unique_ptr<std::lock_guard<std::mutex>>> lock_guards;
				SkipListNode *pred = nullptr, *succ = nullptr, *prevPred = nullptr;
				bool valid = true;
				for (uint8_t layer = 1; valid && layer <= top_layer; ++layer) {
					pred = preds[layer].get();
					succ = succs[layer].get();
					if (pred != prevPred) {
						lock_guards.push_back(
							std::unique_ptr<std::lock_guard<std::mutex>>(
								new std::lock_guard<std::mutex>(
									pred->mutex
								)
							)
						);
						prevPred = pred;
					}
					valid = !pred->marked && pred->forward[layer].get() == succ;
				}
				if (!valid) {
					continue;
				}
				for (uint8_t layer = top_layer; layer >= 1; --layer) {
					preds[layer]->forward[layer] = node_to_delete->forward[layer];
				}
				node_to_delete->mutex.unlock();
				return true;
			}
		} else {
			return false;
		}
	}
}

void SkipList::insert(uint64_t key, const std::string& value) {
	std::vector<SkipListNode*> update(_max_level+1);
	SkipListNode *x = _head.get();
	for (size_t i = _level; i >= 1; --i) {
		while (x->forward[i]->key < key) {
			x = x->forward[i].get();
		}
		update[i] = x;
	}
	x = x->forward[1].get();
	if (x->key == key) {
		x->value = value;
	} else {
		uint8_t lvl = randomLevel();
		if (lvl > _level) {
			for (size_t i = _level+1; i <= lvl; i++) {
				update[i] = _head.get();
			}
			_level = lvl;
		}
		auto p = SkipList::makeNode(lvl, key, value);
		std::shared_ptr<SkipListNode> sp = std::move(p);
		for (size_t i = 1; i <= lvl; ++i) {
			sp->forward[i] = update[i]->forward[i];
			update[i]->forward[i] = sp;
		}
	}
}

bool SkipList::erase(uint64_t key) {
	std::vector<SkipListNode*> update(_max_level+1);
	SkipListNode *x = _head.get();
	for (size_t i = _level; i >= 1; --i) {
		while (x->forward[i]->key < key) {
			x = x->forward[i].get();
		}
		update[i] = x;
	}
	x = x->forward[1].get();
	if (x->key != key) {
		return false;
	}
	for (size_t i = 1; i <= _level; ++i) {
		if (update[i]->forward[i].get() != x) {
			break;
		}
		update[i]->forward[i] = x->forward[i];
	}
	while (_level > 1 && _head->forward[_level]->key == std::numeric_limits<uint64_t>::max()) {
		--_level;
	}
	return true;
}

}
