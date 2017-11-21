#include <iostream>
#include <cstdlib>
#include <list>
#include <map>
#include <assert.h>
#include "skiplist.h"

SkipListNode::SkipListNode(uint64_t key, const std::string& value, uint8_t level)
	:key(key),
	 value(value),
     forward(level+1) {
#ifdef DEBUG
	alive.fetch_add(1);
#endif
}

SkipListNode::~SkipListNode() {
#ifdef DEBUG
	alive.fetch_sub(1);
#endif
};

std::atomic<size_t> SkipListNode::alive(0);

std::unique_ptr<SkipListNode> SkipList::makeNode(uint8_t lvl,
		uint64_t key, const std::string& value) {
	return std::unique_ptr<SkipListNode>(new SkipListNode(
			key,
			value,
			lvl));
}

// head->  NilNode{INF, "", 0}  (level 4)
//     ->  NilNode{INF, "", 0}  (level 3)
//     ->  NilNode{INF, "", 0}  (level 2)
//     ->  NilNode{INF, "", 0}  (level 1)
SkipList::SkipList(uint8_t max_level)
	:_max_level(max_level),
	 _level(1) {
	// key,value for headnode is meanless
	_head = SkipList::makeNode(max_level, 0, "");
	for (size_t i = 0; i <= max_level; i++) {
		_head->forward[i] = SkipList::makeNode(0, std::numeric_limits<uint64_t>::max(), "");
	}
}

uint8_t SkipList::randomLevel() const {
	uint8_t lvl = 1;
	while(rand() % 2 && lvl < _max_level) {
		++lvl;
	}
	return lvl;
}

bool SkipList::findNode(uint64_t key, std::vector<SkipListNode*>* preds,
		std::vector<std::shared_ptr<SkipListNode>>* succs, uint8_t* layer) {
	SkipListNode *prev = _head.get();
	bool found = false;
	assert(preds->size() >= _max_level+1);
	assert(succs->size() >= _max_level+1);
	for (size_t i = _max_level; i >= 1; --i) {
		std::shared_ptr<SkipListNode> curr = prev->forward[i];
		while (curr->key < key) {
			prev = curr.get();
			curr = curr->forward[i];
		}
		if (!found && curr->key == key) {
			found = true;
			*layer = i;
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
	std::vector<SkipListNode*> preds(_max_level+1);
	std::vector<std::shared_ptr<SkipListNode>> succs(_max_level+1);
	// a list of unique_ptr wrapped lockgurad
	std::list<std::unique_ptr<std::lock_guard<std::mutex>>> lock_guards;
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
		SkipListNode *pred = nullptr, *succ = nullptr, *prevPred = nullptr;
		bool valid = true;
		for (uint8_t layer = 1; valid && layer <= top_layer; ++layer) {
			pred = preds[layer];
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
			p->forward[layer] = succs[layer];
			preds[layer]->forward[layer] = sp;
		}
		sp->fullyLinked.store(true);
		return true;
	}
}

bool SkipList::okToDelete(SkipListNode *node, uint8_t found_level) {
	return (node->fullyLinked.load() && node->forward.size() == found_level && (!node->marked));
}

bool SkipList::concurrentContains(uint64_t key) {
	std::vector<SkipListNode*> preds(_max_level+1);
	std::vector<std::shared_ptr<SkipListNode>> succs(_max_level+1);
	uint8_t found_level = 0;
	bool found = findNode(key, &preds, &succs, &found_level);
	return (found && succs[found_level]->fullyLinked.load() && !succs[found_level]->marked.load());
}

bool SkipList::concurrentErase(uint64_t key) {
	std::vector<SkipListNode*> preds(_max_level+1);
	std::vector<std::shared_ptr<SkipListNode>> succs(_max_level+1);
	std::shared_ptr<SkipListNode> node_to_delete;
	std::list<std::unique_ptr<std::lock_guard<std::mutex>>> lock_guards;
	bool is_marked = false;
	int32_t top_layer = -1;
	while(true) {
		uint8_t found_level = 0;
		bool found = findNode(key, &preds, &succs, &found_level);
		if (is_marked || (found && okToDelete(succs[found_level].get(), found_level))) {
			if (!is_marked) {
				node_to_delete = succs[found_level];
				top_layer = node_to_delete->forward.size();
				node_to_delete->mutex.lock();
				if (node_to_delete->marked.load()) {
					node_to_delete->mutex.unlock();
					return false;
				}
				node_to_delete->marked.store(true);
				is_marked = true;
			}
			SkipListNode *pred = nullptr, *succ = nullptr, *prevPred = nullptr;
			bool valid = true;
			for (uint8_t layer = 1; valid && layer <= top_layer; ++layer) {
				pred = preds[layer];
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

void atexit_handler_1() 
{
    std::cout << SkipListNode::alive.load() << std::endl;
}

void insert_op(SkipList *sl, std::map<std::uint64_t, std::string>* m) {
	uint64_t key = static_cast<uint64_t>(rand() % 1000);
	sl->insert(key, "");
	(*m)[key] = "";
}

void delete_op(SkipList *sl, std::map<std::uint64_t, std::string>* m) {
	uint64_t key = static_cast<uint64_t>(rand() % 1000);
	assert((sl->erase(key)) == static_cast<bool>(m->erase(key)));
}

void find_op(SkipList *sl, std::map<std::uint64_t, std::string>* m) {
	uint64_t key = static_cast<uint64_t>(rand() % 1000);
	assert((sl->contains(key)) == (m->find(key) != m->end()));
}

int main() {
	std::atexit(atexit_handler_1);
	srand(time(NULL));
	std::unique_ptr<SkipList> p(new SkipList(10));
	std::map<std::uint64_t, std::string> m;
	for (uint32_t i = 0; i < 10000000; ++i) {
		int op = rand()%3;
		switch(op) {
			case 0:
				insert_op(p.get(), &m);
				break;
			case 1:
				delete_op(p.get(), &m);
				break;
			case 2:
				find_op(p.get(), &m);
				break;
			default:
				break;
		}
	}
	return 0;
}
