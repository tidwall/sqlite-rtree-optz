# sqlite-rtree-optz

This repo includes a fork of the Sqlite amalga file with the following optimzations to the R-tree module:

- When choosing a candidate for insertion, the first rect that does not incur any enlargement at all is immediately chosen and without further checks on other rects in the same node.
- Added a [path hint](https://github.com/tidwall/btree/blob/master/PATH_HINT.md) to track the node path used by the last inserted item. This path is a hint for the next insert, falling back to choosing a candidate using the standard loop-style logic.
- Disable reinsertions

Here's the [diff](https://github.com/tidwall/sqlite-rtree-optz/commit/04a2aef).

These changes will generally speed up the insertion of bulk data when the data has some order to it, such as with space-filling curves like Hilbert.

To enable use `-DSQLITE_RTREE_OPTZ=1`.

## Benchmarks

The following benchmarks were run on Ubuntu 20.04 (3.4GHz 16-Core AMD Ryzen 9 5950X) using gcc-11.

One million random (evenly distributed) points are inserted. 
Then the tree is seached for items that intersect a 1%, 5%, and 10% window of the tree's MBR.

Testing the performance of inserting in random order and [hilbert order](https://en.wikipedia.org/wiki/Hilbert_curve). The hilbert "insert" benchmark includes both the sorting of the data and the insertion into the Sqlite database.

### No optimizations

```
$ cc -DSQLITE_ENABLE_RTREE=1 -O3 -o nooptz sqlite3.c bench.c
$ ./nooptz
SEED=1165218503 N=1000000 TWINDOW=100000
-- RANDOM ORDER --
insert          1,000,000 ops in 9.164 secs   9163.9 ns/op     109,123 op/sec
search-1%           1,000 ops in 0.020 secs  19825.0 ns/op      50,441 op/sec
search-5%           1,000 ops in 0.186 secs 185540.0 ns/op       5,389 op/sec
search-10%          1,000 ops in 0.637 secs 637440.0 ns/op       1,568 op/sec
-- HILBERT ORDER --
insert          1,000,000 ops in 6.419 secs   6418.9 ns/op     155,788 op/sec
search-1%           1,000 ops in 0.019 secs  18665.0 ns/op      53,576 op/sec
search-5%           1,000 ops in 0.161 secs 161348.0 ns/op       6,197 op/sec
search-10%          1,000 ops in 0.537 secs 537189.0 ns/op       1,861 op/sec
```

### With optimizations


```
$ cc -DSQLITE_ENABLE_RTREE=1 -DSQLITE_RTREE_OPTZ=1 -O3 -o withoptz sqlite3.c bench.c
$ ./withoptz
SEED=1270263122 N=1000000 TWINDOW=100000
-- RANDOM ORDER --
insert          1,000,000 ops in 5.906 secs   5906.3 ns/op     169,310 op/sec
search-1%           1,000 ops in 0.019 secs  19014.0 ns/op      52,592 op/sec
search-5%           1,000 ops in 0.182 secs 182202.0 ns/op       5,488 op/sec
search-10%          1,000 ops in 0.622 secs 622024.0 ns/op       1,607 op/sec
-- HILBERT ORDER --
insert          1,000,000 ops in 3.543 secs   3543.3 ns/op     282,225 op/sec
search-1%           1,000 ops in 0.018 secs  17624.0 ns/op      56,740 op/sec
search-5%           1,000 ops in 0.152 secs 152184.0 ns/op       6,570 op/sec
search-10%          1,000 ops in 0.511 secs 510869.0 ns/op       1,957 op/sec
```