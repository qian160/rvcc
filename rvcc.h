#include<stdarg.h>
#include<stdio.h>

// 输出错误信息
// Fmt为传入的字符串， ... 为可变参数，表示Fmt后面所有的参数
//__attribute__((unused))
//static void error(char *Fmt, ...) {
//    va_list VA;
//    // VA获取Fmt后面的所有参数
//    va_start(VA, Fmt);
//    // vfprintf可以输出va_list类型的参数
//    vfprintf(stderr, Fmt, VA);
//    // 在结尾加上一个换行符
//    fprintf(stderr, "\n");
//    // 清除VA
//    va_end(VA);
//    // 终止程序
//    exit(1);
//}

// 为每个终结符(token)都设置种类来表示
typedef enum {
    TK_PUNCT, // 操作符如： + -
    TK_NUM,   // 数字
    TK_EOF,   // 文件终止符，即文件的最后
} TokenKind;

// 终结符结构体
typedef struct Token Token;
struct Token {
    TokenKind Kind; // 种类
    Token *Next;    // 指向下一终结符
    int Val;        // 值
    char *Loc;      // 在解析的字符串内的位置
    int Len;        // 长度
};


#define error(format, ...) \
    do{ \
        printf("\33[1;31m" "[%s:%d %s] " format "\33[0m" "\n", \
            __FILE__, __LINE__, __func__, ## __VA_ARGS__);\
        exit(1);\
    }\
    while(0);