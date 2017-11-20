#include <iostream>
#include <cstdlib>
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
		}
		auto p = SkipList::makeNode(lvl, key, value);
		for (size_t i = 1; i <= _level; ++i) {
			p->forward[i] = update[i]->forward[i];
			update[i]->forward[i] = std::move(p);
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
	std::unique_ptr<SkipList> p(new SkipList(6));
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
