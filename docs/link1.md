# 环境介绍
1. 当前目录是`/home/assit/hk2`，你的结果、生成的输出文件，可以放在这个目录中。
2. 如果遇到没有权限错误，你可以在/home/assit/hk2中写入结果信息，并把赋权命令也写入其中，还有我赋权后如何将你继续执行的命令。我会授于相应权限后，把你继续运行起来。
3. 把你所有的输出写入当前目录`/home/assit/hk2`中，以当前日期为文件名，比如`0910_1.log`。
4. 工作环境是Linux，使用gcc编译程序，可以使用-O2级优化，但不要使用-O3。

# 任务描述
1. 使用C语言写一个链表程序，链表数据结构如下：
typedef struct LinkData
{
    struct LinkData     *prev;
    struct LinkData     *next;
    int     type;
    int     dataSize;
    char    *data;
} LinkData;
2. 双向链表。
3. 数据结构的大小，最小32字节，最大512字节。
4. 链表节点数目有可能非常多，多达上万个节点。
5. 链表的条数将会更多，多达数百万条链表、千万条链表。
6. 程序中要包含如下子函数：
LinkData *InitLink(int size, char *mem)：初始化链表子函数
void AppendNode(LinkData *firstNode，int size, char *mem)：向链表中追加节点的函数
LinkData *FindNode(LinkData *firstNode，int size, char *mem)：链表搜索子函数
7. InitLink()的要求：
它创建一个链表头、并返回链表头。
链表头可以这样定义：`LinkData *firstNode`
参数size就是结构成员firstNode->dataSize的值，也是成员firstNode->data指针指向的内存大小。
参数mem是要写入firstNode->data的数据。
做为链表第一个Node，它的prev、next指向自己。
8. AppendNode()的要求：
它在最后一个节点后增加新的节点。
firstNode->prev会一直指向最后一个节点，因此增加新的节点后，要修改firstNode->prev，保持它指向最后的节点。
原来最后的节点的next也要被修改。
参数size是参数mem所指向内存的大小。
参数mem是要写入Node->data的数据。
9. FindNode()的要求：
它从头开始，遍历链表，搜索目标节点，返回目标节点的地址。
要搜索的数据在参数mem中，大小在参数size中。
10. 要以最快的性能，实现链表搜索函数。

# 任务要求
1. 搜索的性能是这个程序的关键。你要想尽一切手段，提升搜索的性能。
2. 如果需要，你可以重构`LinkData`结构，只要它包含`dataSize`和`data`就行。
3. 你可以使用lscpu，获得主机的CPU类型等信息，面向主机硬件进行专门的优化，也是必要的，不需要考虑通用性。
4. 如果需要，`以空间换时间`等手段也可以使用。多占一些内存空间是可以接受的，只要搜索性能可以更快。

**注意事项**
1. 结果写入当前目录`/home/assit/hk2`，不要修改和删除其他目录中的文件。
