#ifndef __SKIPLIST_H__
#define __SKIPLIST_H__

#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <atomic>

#define DEBUG 1

struct SkipListNode {
	SkipListNode(uint64_t key, const std::string& value, uint8_t level);
	~SkipListNode();
	uint64_t key;
	std::string value;
	// shared by preceding nodes
	std::vector<std::shared_ptr<SkipListNode>> forward;
	static std::atomic<size_t> alive;
};

class SkipList {
public:
	SkipList(uint8_t max_height);
	void insert(uint64_t key, const std::string& value);
	bool erase(uint64_t key);
	bool contains(uint64_t key); 
	uint8_t randomLevel() const;
	static std::unique_ptr<SkipListNode> makeNode(uint8_t lvl, uint64_t key,
			const std::string& value);
private:
	const uint8_t _max_level;
	uint8_t _level; // current level
	std::unique_ptr<SkipListNode> _head;
};

#endif // __SKIPLIST_H__
