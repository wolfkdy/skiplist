# skiplist
### a straightforward c++ implementation of William Pugh's paper(http://epaperpress.com/sortsearch/download/skiplist.pdf)
### concurrent interface is based on https://www.cs.tau.ac.il/~shanir/nir-pubs-web/Papers/OPODIS2006-BA.pdf
```
后续优化思路：
1）按照论文，该concurrentSkipList 是静态高度(max_height)的，在查询时比90年的第一版论文有更多的常熟消耗，这里应该可以优化
2）mutex -> spinLock， 这里mutex是为了快速实现，这种临界区很小的而且没有IO操作的，用spinLock更好，但是要实测
3）shared_ptr管理内存优化，这里为何用shared_ptr，因为 findNode 是LockFree的，返回的pred 和succ 数组可能被其它线程delete掉，所以需要对节点进行引用计数。可能可替代的方向是harzardpoint。
4）为了保证与论文严格的一致性，fullyLinked字段和mark 字段都是用atomic.store和atomic.load，c++在这里是使用默认的memory_order_seq_cst保证变量更改对其他线程最低的乱序性。这两个字段是否需要atomic需要进一步研究论文，这里水很深，除了6种c++封装的memorder之外，比较好的paper是http://www.rdrop.com/~paulmck/scalability/paper/whymb.2010.06.07c.pdf. 这块，做系统内核的人可能比较了解。我使用起来需要翻着书做。
```
