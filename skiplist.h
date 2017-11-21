#ifndef __SKIPLIST_H__
#define __SKIPLIST_H__

#include <mutex>
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

	std::mutex mutex;

	// TODO: make sure marked and fullyLinked is protected by mutex
	//	if so, atomic is no need since mutex is a sequencial-barrier
	std::atomic<bool> marked;

	std::atomic<bool> fullyLinked;	

	static std::atomic<size_t> alive;
};

class SkipList {
public:

	SkipList(uint8_t max_height);

	void insert(uint64_t key, const std::string& value);

	bool concurrentInsert(uint64_t key, const std::string& value);

	bool concurrentErase(uint64_t key);

	bool concurrentContains(uint64_t key);

	bool erase(uint64_t key);

	bool contains(uint64_t key); 

	uint8_t randomLevel() const;

private:
	const uint8_t _max_level;

	// current level, not used in concurrent version
	uint8_t _level;

	bool findNode(uint64_t key, std::vector<SkipListNode*>* preds,
			std::vector<std::shared_ptr<SkipListNode>>* succs, uint8_t* layer);

	static std::unique_ptr<SkipListNode> makeNode(uint8_t lvl, uint64_t key,
		const std::string& value);

	static bool okToDelete(SkipListNode *node, uint8_t found_level);

	std::unique_ptr<SkipListNode> _head;
};

#endif // __SKIPLIST_H__
