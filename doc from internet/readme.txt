

The Google File System                                                    HDFS
MapReduce: Simplified Data Processing on Large Clusters                  MapReduce
Bigtable: A Distributed Storage System for Structured Data               HBase

The Chubby lock service for loosely-coupled distributed systems          基于Paxos协议  zookeeper


Raft。Raft是14年提出的一个一致性协议。用来取代Paxos，因为后者实在太复杂，太难以理解。（lamport表示你们都是渣渣）。分布式系统一个经典的模型就是副本状态机，Raft就是用来维护这个副本状态机的一致性的。
2 phase commit，两阶段提交协议。这是分布式事务环境下，如何确保多个机器上事务同时提交或者失败的一个协议。
Distributed Snapshot。分布式系统快照。这也是非常经典的一个分布式问题，因为分布式系统做快照的时候，各个机器不同步，加上有些信息在网络上飞，所以如何得到一个正确的快照是一个很难的问题。这篇文章提出一个可以理论证明是正确的解。


Distributed theory
1982年Symposium on Principles of Distributed Computing (PODC)正式成立[4]
1985年二阶段提交协议被Mohan, Bruce Lindsay发明[7]
1989年Lamport又提出了饱受争议的Paxos协议[9]
2005年Lamport对其Paxos算法的改进版本Fast Paxos被发明
2013年，参考了Paxos算法实现的RAFT算法被发明[15]             ZooKeeper,Etcd


CAP理论即Consistency（一致性）、 Availability（可用性）、Partition tolerance（分区容错性）     raft算法就是cp的一种实现