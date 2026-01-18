#include <stdio.h>
#include <string.h>

int main() {
    char buf[256];
    
    // 测试 sprintf
    sprintf(buf, "Hello %s", "World");
    printf("Test 1: %s\n", buf);
    
    sprintf(buf, "Number: %d", 42);
    printf("Test 2: %s\n", buf);
    
    sprintf(buf, "Hex: 0x%x", 255);
    printf("Test 3: %s\n", buf);
    
    sprintf(buf, "Char: %c", 'A');
    printf("Test 4: %s\n", buf);
    
    sprintf(buf, "Percent: %%");
    printf("Test 5: %s\n", buf);
    
    sprintf(buf, "%d + %d = %d", 1, 1, 2);
    printf("Test 6: %s\n", buf);
    
    // 测试 printf
    printf("Direct printf: %s %d 0x%x\n", "test", 123, 0xABC);
    
    return 0;
}
