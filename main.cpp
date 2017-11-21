#include <thread>
#include <iostream>
#include <map>
#include <assert.h>

#include "skiplist.h"

using namespace Concurrent;

void atexit_handler_1() 
{
    assert(SkipListNode::alive.load() == 0);
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

void con_insert(SkipList *p, uint32_t start, uint32_t end) {
	for (uint32_t i = start; i < end; ++i) {
		//std::cout << i << " inserted" << std::endl;
		//p->traverse();
		assert(p->concurrentInsert(i, ""));
	}
}

void con_contains(SkipList *p, uint32_t start, uint32_t end) {
	for (uint32_t i = start; i < end; ++i) {
		assert(p->concurrentContains(i));
	}
}

void con_erase(SkipList *p, uint32_t start, uint32_t end) {
	for (uint32_t i = start; i < end; ++i) {
		assert(p->concurrentErase(i));
	}
}

void nonconcurrent_test() {
	std::unique_ptr<SkipList> p(new SkipList(10));
	std::map<std::uint64_t, std::string> m;
	std::cout << "thread unsafe skiplist test" << std::endl;
	for (uint32_t i = 0; i < 220000; ++i) {
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
}

void concurrent_insert_insert() {
	std::unique_ptr<SkipList> p(new SkipList(20));
	const size_t insert_num = 100000;
	std::thread t1(con_insert, p.get(), 0, insert_num);
	std::thread t2(con_insert, p.get(), insert_num, insert_num*2);
	t1.join();
	t2.join();
	std::thread t3(con_contains, p.get(), 0, insert_num);
	std::thread t4(con_contains, p.get(), insert_num, insert_num*2);
	t3.join();
	t4.join();
}

void concurrent_insert_contains() {
	std::unique_ptr<SkipList> p(new SkipList(20));
	const size_t insert_num = 100000;
	std::thread t1(con_insert, p.get(), 0, insert_num);
	t1.join();
	std::thread t2(con_contains, p.get(), 0, insert_num);
	std::thread t3(con_insert, p.get(), insert_num, insert_num*2);
	t2.join();
	t3.join();
}

void concurrent_insert_erase() {
	std::unique_ptr<SkipList> p(new SkipList(20));
	const size_t insert_num = 100000;
	std::thread t1(con_insert, p.get(), 0, insert_num);
	t1.join();
	//p->traverse();
	//std::cout << p->concurrentErase(0) << std::endl;
	std::thread t2(con_erase, p.get(), 0, insert_num);
	std::thread t3(con_insert, p.get(), insert_num, insert_num*2);
	t2.join();
	t3.join();
}

int main() {
	std::atexit(atexit_handler_1);
	concurrent_insert_insert();
	concurrent_insert_contains();
	concurrent_insert_erase();
	return 0;
}

