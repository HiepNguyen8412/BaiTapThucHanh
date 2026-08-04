#include <cstdio>
#include <cstdlib>
#include <cstdint>

int GlobalVar = 10;

uintptr_t Distance(uintptr_t a, uintptr_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

void ExploreMemory()
{
    int LocalVar = 20;
    static int StaticVar = 30;

    int* HeapVar = new int(40);

    uintptr_t global = (uintptr_t)&GlobalVar;
    uintptr_t stat = (uintptr_t)&StaticVar;
    uintptr_t heap = (uintptr_t)HeapVar;
    uintptr_t local = (uintptr_t)&LocalVar;

    printf("============== MEMORY LAYOUT ==============\n\n");

    printf("Global Variable\n");
    printf("Value   : %d\n", GlobalVar);
    printf("Address : 0x%016llX\n\n", (unsigned long long)global);

    printf("Static Variable\n");
    printf("Value   : %d\n", StaticVar);
    printf("Address : 0x%016llX\n\n", (unsigned long long)stat);

    printf("Heap Variable\n");
    printf("Value   : %d\n", *HeapVar);
    printf("Address : 0x%016llX\n\n", (unsigned long long)heap);

    printf("Local Variable\n");
    printf("Value   : %d\n", LocalVar);
    printf("Address : 0x%016llX\n\n", (unsigned long long)local);

    printf("========== ADDRESS DISTANCE ==========\n");

    printf("Global <-> Static : 0x%llX (%llu bytes)\n",
        (unsigned long long)Distance(global, stat),
        (unsigned long long)Distance(global, stat));

    printf("Static <-> Heap   : 0x%llX (%llu bytes)\n",
        (unsigned long long)Distance(stat, heap),
        (unsigned long long)Distance(stat, heap));

    printf("Heap   <-> Local  : 0x%llX (%llu bytes)\n",
        (unsigned long long)Distance(heap, local),
        (unsigned long long)Distance(heap, local));

    printf("\n========== OBSERVATION ==========\n");
    printf("Global/Static : Data Segment\n");
    printf("Heap          : Heap Segment (mo rong ve phia dia chi lon hon)\n");
    printf("Local (Stack)         : Stack Segment (Di chuyển xuống các địa chỉ nhỏ hơn)\n");

    delete HeapVar;
}

int main()
{
    ExploreMemory();
    return 0;
}