学习过程中做的一些笔记，一起上传上来好了。<br>

## 工作流程：

​		总的来说分为四步：词法分析->预处理->语法分析->代码生成

### 词法分析

​		接口：**tokenize**

​		将输入当作是一个字符串。先通过**tokenize**函数遍历并从这堆字符串中提取出每个token，串到一个链表当中。同时给每个token添加一些辅助信息例如类型、数值(如果类型是数字的话)、位置、长度。提取完之后最后再遍历一次将相应的节点转化关键字，这样这些节点在后续就不会被误判为标识符什么的了。除此以外不做太多别的工作。

### **预处理**

在tokenize获取到初始的tokens链表之后，进一步对他做一些加工处理。主要就是从头开始再遍历一遍tokens链表，然后：

- 处理以#开头的指示性指令
- 宏替换。这里的关键在于expandMacro和subst函数

#### **宏结构体**

重要成员变量主要有：

- name：用于标识一个宏

- body：宏的内容部分，由一串tokens组成。例如
  ```c
  #define foo(x)  ((x<<12) & 0b11)
  ```

  则他的body就是  ((x<<12) & 0b11)。至于x，会在宏展开时被subst函数替换为具体的东西

- params：参数列表，上例中即为x

#### **隐藏集：**

​		有这样的需求：**一个宏只对每个token应用一次**。否则可能会因为**循环调用**导致死循环之类的。例如以下定义：

```
int M1 = 3;
#define M1 M2 * 5
#define M2 M1 + 2
printf("%d\n", M1);		// 3 + 2 * 5 = 13
```

​	`printf`里的参数M1就是这里所说的token。其分别各使用了一次M1和M2这两个宏

隐藏集的使用：

​		在函数`expandMacro`中，会首先检查**当前token**是否已经展开过了。这通过hidesetContains函数来完成，他获取当前token持有的隐藏集和token的名字，然后遍历那个隐藏集逐个比较。如果包含在隐藏集中说明是展开过了，那么就不再展开。否则展开。处理完后还要做两件事：1.把刚刚展开过的那个宏的名字添加到当前token的隐藏集中2.还要将那个隐藏集往后面的token传递，因为不仅是当前token，之后的所有token也不能再展开那个宏。这里的“之后的token”指的是宏的body

#### **宏的相关解析：**

定义时：

preprocess函数遍历tokens链表时，若发现满足两个条件：

- 位于行首的是#
- #后面跟的是define

则接下来的**整行**就会被当成宏定义来解析。构建一个macro结构添加到全局的链表中，并给他解析出形参，名字，body等信息

使用时：

这步主要由macroExpand函数来完成。分两种情况：宏变量和宏函数。其实就是一个替换的工作。取出之前存在宏的body里的那串tokens，把他接入到当前的链表当中。这里涉及到类似链表插入的操作

```c
#define foo 1+9
int x = 1 + foo + 9;

/*
	原tokens:
		当前正在处理的token
			   ↓
	1 -> + -> (foo) -> + -> 9
	替换后：
	1 -> + -> (1 -> + -> 9) -> + -> 9
	
	(这里的括号是为了看得稍微清楚一些才加的)
	于是这一操作可以总结为:
	1.把原token替换为body。注意这里并不是直接对指针做一次替换，而是新建了一串tokens然后用循环把body里面的tokens一个个复制过去，然后再把新建的这串tokens接到原token的位置。不能直接做指针替换是因为之后还要做parse，若该宏被使用了多次，只做指针替换的话会导致所有使用了该宏的地方，body的后一个token都相同。更具体地说，前面使用到的宏的尾部，会指向后面用到宏的结尾。对于前面的宏这将会让他跳过之间的很多解析，而且逻辑也不对
	2.把原token的结尾(这里是'+')接到新token的后面
*/
```

#### **"内置函数"**

#### **可变参数`__VA_ARGS__`**

#### **内置宏**

##### **`__func__`**

https://gcc.gnu.org/onlinedocs/gcc/Function-Names.html

GCC provides three magic constants that hold the name of the current function as a string. In C++11 and later modes, all three are treated as **constant expressions** and can be used in `constexpr` constexts. The first of these constants is `__func__`, which is part of the C99 standard:

The identifier `__func__` is implicitly declared by the translator as if, immediately following the opening brace of each function definition, the declaration

```
static const char __func__[] = "function-name";
```

appeared, where function-name is the name of the lexically-enclosing function. This name is the unadorned name of the function. As an extension, at file (or, in C++, namespace scope), `__func__` evaluates to the empty string.

`__FUNCTION__` is another name for `__func__`, provided for backward compatibility with old versions of GCC.

These identifiers are **variables**, not preprocessor macros, and may not be used to initialize `char` arrays or be concatenated with string literals.

其实不是在预处理阶段通过文本替换完成的，而是在语法分析parse阶段被作为变量使用，(可通过gcc -E观察正常程序，发现`__func__`并不会被替换)

实现主要围绕两个问题：

1.是放到全局变量还是局部变量

2.由哪个模块的哪个函数来负责相关解析工作

关于问题1，局部变量看似似乎更合理一些，但是处理起来麻烦。因为一个函数在解析完后栈大小和布局已经确定，此时再去做一些微操作就比较麻烦。而放到全局则处理十分简单。所以干脆放到全局，似乎c标准里也是这么做的。

关于2，语法分析阶段比预处理知道的信息要更多，所以更适合来干这一步。能胜任的相关函数则是primary或者function，这里选择了后者，两者之间似乎没什么明显区别

##### `__FILE__`

##### **`__LINE__`**

#### **subst函数**

​		

#### **expandMacro函数**

​		返回一个bool值，若当前的token不是宏或者已经处于隐藏集则不做任何处理直接返回false，否则就调用subst做一些替换工作。

### **指示**

#### **include**

​		如果当前token是#include，那么调用tokenizeFile去解析那个文件，然后将返回的token流“接入”原token的后面，其实就是实现了复制粘贴的效果。这里的巧妙之处是，实际上没有做任何直接对文件的修改就达到了这种复制粘贴的效果

#### **if/endif/elif**

​		todo。合法性检查，主要是不能出现stray现象

#### **define/undef**

​		当匹配到define关键字时，就新建一个struct macro，名字是define后面跟的那个，内容则使用copyline函数获取该行剩下的内容。并在末尾添加**EOF**截断。下次遇到的时候就**展开**他（其实就是把之前存储在那个macro里面的token**接到**当前token的后面，类似于复制粘贴）



##### **带参数的宏：**

​		新增`readMacroDefination`函数，用于识别不同类型的宏。识别方法也很巧妙，当前token是一个标识符，作为宏的名字。如果是函数风格的宏，那么下一个token必然是 `(` ，并且和宏名字是连在一起的中间没有空格。抓住这两点就能区分了。

​		值得注意的是宏变量的内容也可以以`(`作为开头，只不过这里的括号前面是有空格的

识别过程：

​		todo



​		undef则比较简单粗暴，利用了栈的特性，直接新添加一个同名宏，但内容为空而且标记为deleted。这样下次findMacro的时候会先找到那个新添加进去的宏，发现其为deleted就返回NULL。

​		这样做浪费了空间，但省去了链表删除的麻烦

#### **ifdef/ifndef**

​	todo

#### **#宏参数字符化操作符**

```c
#define stringize(x) #x
```


​    只能在宏中使用。#后面紧跟的那个token必须是宏函数实参。然后会将其传入stringize函数并做字符串化处理。返回的字符串会被接到当前token的后面

#### **##宏参数拼接操作符**

```c
//用法比字符化操作符#多，允许##的左右不是宏参数。例如以下几种定义都是合法的:
#define cat1(x, y) x##y
#define cat2(x, y) 1##2
#define cat3(x, y) 1##x
#define cat4(x, y) x##1

int xx = 1;
int y = cat1(x,x);	// y = xx = 1
```

处理策略：

todo

- 

### 语义分析

​		接口：**parse**

​		最关键的阶段。根据之前获得的token链表，生成AST并返回解析出的所有**objects**（函数或者全局变量）。如果是函数则还要做很多进一步解析，全局变量则比较轻松直接添加到表示全局变量的链表里就可。函数包含的信息量十分巨大，可视为由很多语句构成。按照一定规则遍历生成这些语句就能生成一个函数。

```
program =
	int fn1(){int a = 3; int b = 4; return a + b;}   
	int fn2(){...}
	int gv;

		obj1(fn1)	->	obj2(fn2)	->	obj3(gv)	->	NULL
		  	/			/  \
		   /		  ...  ...
	COMPOUND_STMT(ND_BLOCK)
		|
		↓
	Body(ND_ASSIGN)  ->  next(ND_ASSIGN)  ->  next(ND_RETURN) -> NULL
       /   		\     	   /   		\    	   	/   (UNARY)
    ND_VAR	   ND_EXPR   ND_VAR		ND_EXPR   ND_EXPR
```

该阶段的具体做法：

- **定义一些推导式**，将输入视为由这些推导式的组合构成。最重要的一步
- 通过解析这些推导式来构建AST。这是一个**递归**的过程。我们的最终目标是获得最上层的推导式，然后上层推导式又是由下层的推导式构成的，有些下层的推导式可能也会用到一些上层推导式。越往下的推导式优先级其实越高。其中最底层的primary推导式大概表示的是一个很小的可用单元，等于一个数值(变量，表达式，字符串字面量，或者一个数字本身)

​		这棵树其实是**立体**的，并不只是简单的二叉树：每个节点不只有left和right孩子，还有`next`、`cond`、`else`、`then`，`body`，`init`，`inc`、`args` 这些特殊的“孩子”。他们是用来服务于一些特殊语句的。具体介绍如下：

- `body`：{ ... } 块语句/代码块 的根节点
- `init`：for语句的初始化部分。
- `inc`：for语句的递增部分。
- `cond`：if / for / while语句的判断条件部分
- `then`：if / for / while语句条件成立时执行的部分，循环体
- `else`：if语句条件不成立时执行的语句(可选)
- `args`：函数被调用时的**实参**。本质上是表达式。
- `next`：配合其他使用，表示下一条语句或下一条表达式。例如当使用body作为根节点时，next表示的就是代码块中的下一条语句；又如使用arg作为根节点时，next又表示下一个实参(表达式)

​		这些成员表示的都是某种**根节点**，或者说**链表头**，指向第一条语句或者表达式。相当于是当前的二叉树节点又指向了另外一颗子树。生成代码时，如果发现节点的类型特殊，就要去参考一下以上列出的这些节点内的东西。

### 代码生成

​		接口：**codegen**

​		该阶段又可细分为3个小步骤：

#### 为本地变量分配栈空间

​		在之前的parse阶段，识别`compoundStmt`时可能会遇到`declaration`。这时候一个本地变量就被添加到了obj->locals中。在`assignLVarOffsets`函数当中会根据每个变量的size为其在栈中计算合理的偏移量，并最终将这些偏移量累加起来得到函数所需的**初始栈大小**

#### 生成数据段

​		主要是放置并初始化一些**全局变量**。来历可能是objects中直接捕获到的声明在函数外部的全局变量，也可能是字符串字面量(匿名全局变量，一次性使用)。目前只支持对字符串字面量的初始化。

​		关于字符串字面量：首先在tokenize中通过 `"` 这个前缀被检测出来，然后那个token的类型被标记为TK_STR。parse中的primary函数遇到这种类型时就会创造并添加一个匿名全局变量放到globals中。类型是`arrayof(tychar, len)`

#### 生成代码段

​		该阶段的任务是：遍历parse中产生的objects，如果发现他是函数，则以**语句**为单位进行逐语句遍历生成。每种语句所需要的信息已经由上一阶段全部生成，这里只要根据语句的类型进行不同的操作即可。这里有个值得注意的地方是：每个函数在进入函数体之前需要**建立起实参到形参的映射**。方法也很简单，传入的参数已经放到了a0，a1...这些寄存器之中，形参则是在解析`typeSuffix`的时候被添加到了本地变量当中，在栈中由偏移量表示。只需要把a0，a1...这些值存入栈中相应的位置，这样后续的函数体中就可以正常使用这些传入的参数了。

## **类型系统**

​		给每个节点添加了类型，在需要的时候可以作为参考。

​		判断类型时主要是用到`declspec`、`declarator`、`typeSuffix`这几个函数，他们之间层层递进，逐步解析出完整的类型。首先由`declspec`解析出最基础的类型，将这个解析出的类型进一步传入`declarator`，最后再传入`typeSuffix`检查后缀。

​		函数和变量的区分：在`typeSuffix`阶段会被判断出来。当`typeSuffix`发现了后缀是括号，代表这是一个函数。会设置Ty->Kind = TY_FUNC，并进一步解析形参。解析完形参之后还要去看看后面是否跟有函数体，有的话进一步解析并包入`compoundStmt`。

```c
//      
//                           declarator
//                               ↑
//                +--------------+-------------+
//                |                funcParams  |
//                |                +----------+|
//                |                |          ||
//        int     **      fn      (int a, int b)  { ... ... }         and the whole thing is a functionDefination
//         ↑               ↑      |            |  |         |
//      declspec         ident    +-----+------+  +----+----+
//                                      ↓              ↓
//                                 typeSuffix     compoundStmt


// declarator = "*"* ( "(" ident ")" | "(" declarator ")" | ident ) typeSuffix


//                                     declarator1(type1)
//                                         ↑
//              +--------------------------+-----------------------------------+
//              |                                                              |
//              |                                                              |
//        int   **    (     *(  * (     foo     )   [6]          )     [6]     )   [6][6][6]
//      declspec            |   |      ident    | typeSuffix3    | typeSuffix2     typeSuffix1
//                          |   |               |                |
//                          |   +-------+-------+                |
//                          |           ↓                        |
//                          |    declarator3(type3)              |
//                          +--------------+---------------------+
//                                         ↓
//                                  declarator2(type2)
```



```
typeSuffix = ( funcParams  | "[" num "]"  typeSuffix)? 
funcParams =  "(" (param ("," param)*)? ")" 
     param = declspec declarator
```

### 数组ND_ARRAY

​		`typeSuffix`在遇到`[]`后会调用`arrayOf`这个函数将type设置为`TY_ARRAY`，并递归解析是否还有更多维度，每调用一次都会以原来的基类作为base，并把size设置为Base->Size * Len。最终可能会生成`arrayof(arrayof(...))`这样的类型。这里的递归是实现多维数组的关键。目前还无法支持函数数组。

```c
Type *arrayOf(Type *Base, int Len){
    Type *Ty = calloc(1, sizeof(Type));
    Ty -> Kind = TY_ARRAY;
    Ty -> Size = Base -> Size * Len;
    Ty -> Base = Base;
    Ty -> ArrayLen = Len;
    return Ty;
}
```

​		这个TY_ARRAY的类型和指针表现得很相似，所以可以进行一些复用，具体实现是通过在unary或者postfix阶段先将其截获并把数组运算转化成**解引用**。由于是解引用实现的，因此最后在codegen的load函数中，得到变量地址后不需要再从那个地址中把值载出来，交给后续专门的解引用这一步来就行。(不然就相当于是在重复解引用了）

​		注意在`genExpr`这个函数里，遇到`ND_DEREF`时他会先生成子树的表达式。一般我们生成表达式遇到变量的时候，会先计算出他在栈中的地址，再load出来，这样得到的才是表达式真正的值。相当于是已经进行了一次解引用

​		目前唯一一个不同点是数组类型不能出现在赋值号的左边(not a lvalue)，而指针可以

### 指针ND_PTR

​		declarator函数最开始做的就是识别指针。会通过一个while循环识别出基础类型后面**所有的**星号，并不断用之前识别出来的type作为新type的基类，从而构建多级指针。

​		关于指针的运算，在`parse.c`中封装了`newAdd`和`newSub`这两个函数，本质上只是调用`newBinary`生成用于加减功能的普通的二叉树节点，但他还会做一些比较智能的后续工作。如果发现是指针+数字，会把那个数字按照ptr->base->size进行**等比例放大**。具体做法就是用一个乘式代替掉原来的数字。例如：(假设p的类型是某种指针)

```
	   +(ND_ADD)
 	   /	   \
 	  /			\
 P(ND_VAR) 		3(ND_NUM)
```

会被转化成：

```
		 +
	   /    \
	  /      \
	 P 		 *(ND_MUL)
	 	    /   \
	 	   3	P->Ty->Base->Size(ND_NUM)
```



#### 解引用ND_DEREF

​		是一个unary单叉树节点，只会对指针使用。这里考虑到了这样的事实：指针变量本身也只是一个值，解引用其实就是先获取指针变量的值，然后将那个值视作地址再去访问一次内存。所以流程就是：（在genexpr函数中）

```c
case ND_DEREF:
	genExpr(Nd->LHS);
	load(Nd->Ty);
	return;
```



### **结构体**

匿名结构体

todo

### **浮点数**

​		todo

### **函数指针**

**识别：**

​		postfix中，解析出primary后会检查后面一个token是不是  `(` 。是的话就新建一个ND_FUNCALL单叉树，并把刚刚解析出来的那个primary作为左孩子。这主要是为了获取变量的地址

处理：

​		funcall现在不止要在scope中寻找函数了。第三个参数里包含了函数名和类型信息。postfix解析出primary节点后将其作为此参数传入。

​		现在的primary函数也不再解析函数调用了(只解析函数名)。具体的工作被交给了postfix。

不完整类型：

​		主要是数组、结构体。也会被添加到域中，但特别地，我们事先将他们的size设置为了-1，这样之后就可以根据这个知道不能用这个作为类型来使用。



## **作用域：**

​		域是存在嵌套关系的，内层的域可以访问到外层，越往深的域生存的也越短。这适合使用头插法来创建链表。用scope数据结构来表示一个域(花括号)内的所有**标识符**，scope->next表示下一个域，其实是外层的域。scope->vars表示域内的各种标识符，包括函数、变量、别名等。还有用来表示结构体和联合体的标签。

​		question：varscope的name域似乎有些多余？直接用obj->varu不是就可以了吗？目前是这么理解：有时候我们只想往域中放一些“标识符”，例如别名。而不是实打实的变量。如果什么都当成变量放进去可能会给后续带来一些管理上的问题。还有，primary函数解析变量后会加上一句判断S->Var要存在，这也隐含了：找到的这个名字并不属于变量

```C
// scope1
int a;
int main(){
	// scope2
	int b, c;
    typedef int intt;
}

//	 			    			(next)
//			scope1 				-> 					scope2		-> 	NULL
//			/										/
//	   	   / (scp->vars)							/ 
//		 vscp1 		-> 		vscp2	-> NULL		 vscp1 -> ...
//   	/    \				/    \				/    \
// var=main  typedef=NULL   a     NULL			intt   TyInt
```



​		`varscope`看名字好像是一个域，里面应该有很多变量，但其实**一个`varscope`只管理了一个变量**。。。这个命名似乎有点迷惑性。不过既然一个`Varscope`可能表示的东西有那么多，我们该如何区分？其实不做区分，我们只要保证在正确的时候做正确的事情。就可以避免误解析。例如`parseTypeddef`函数中，调用完`declarator`之后得到的肯定是别名，

​		primary函数最后会尝试解析标识符并当作变量，看似有把别名错误解析成变量的风险，但是在`compoundStmt`中会先遇到到那个别名，并触发`IstypeName`。从而继续解析声明，不会往解析变量的方向走

## **函数调用**

### **解析工作**

​	todo

### **调用约定**

->[abi文档](https://wiki.riscv.org/display/HOME/RISC-V+Technical+Specifications)<-

注：我们做的是RV64，所以**ABI_XLEN=64 = ABI_FLEN=64**（取决于double类型的长度）。以下简称XLEN和FLEN

#### **整型传递规则：**

- 参数不超过XLEN位的，若寄存器够用则用**一个**寄存器传递，否则用栈。
- 当参数大小=2*XLEN时，使用**一对**寄存器来传递。低XLEN位存储在编号小的寄存器里，高XLEN位存储在编号大的那个寄存器里。若寄存器不够用则依然是通过栈传递。
  特殊情况：可用寄存器只有一个。那么会把参数拆成两半，低XLEN位放到寄存器里，高XLEN位放到栈上。
- 当参数大于2*XLEN时，选择**传引用**。数据本身放到栈上，然后把参数替换为一个指向它的地址。寄存器够用时就会让那个寄存器里面存储地址，否则把这个地址也压到栈上。

> ​	Scalars wider than 2×XLEN bits are passed by reference and are replaced in the argument list with the address.

​		寄存器足够，且参数能存在一个寄存器内时(≤XLEN)，使用a0-a7这几个寄存器来传递参数，否则用栈。当参数的位数小于寄存器宽度时，根据类型对其做符号扩展或者零扩展至XLEN位。

#### **浮点传递规则**

总的说来，就是“**先参照浮点的传递规则，不行的再去参考整型的”**。而浮点的传递规则比较简单就是依次使用fa0-fa7这几个浮点寄存器，如果能存下的话。注意当去参考整型的传递规则时，意味着浮点数也有可能会通过整型寄存器传递，或者栈

> The hardware floating-point calling convention adds eight floating-point argument registers, fa0- fa7, the first two of which are also used to return values. Values are passed in floating-point registers whenever possible, whether or not the integer registers have been exhausted.
>
> A real floating-point argument is passed in a floating-point argument register if it is no more than ABI_FLEN bits wide and at least one floating-point argument register is available. Otherwise, it is passed according to the integer calling convention. When a floating-point argument narrower than FLEN bits is passed in a floating-point register, it is 1-extended (NaN-boxed) to FLEN bits.

#### **结构体传递规则**（特殊）

前面的浮点和整型传递规则中，提到了参数大于等于2XLEN或2FLEN的情况。正常的整型或者浮点型是肯定不会超过这个限制的，这几条规则其实是对结构体制定的

> A struct containing just one floating-point real is passed as though it were a standalone floating point real.
>
> A struct containing two floating-point reals is passed in two floating-point registers, if neither real is more than ABI_FLEN bits wide and at least two floating-point argument registers are available. (The registers need not be an aligned pair.) Otherwise, it is passed according to the integer calling convention. 
>
> A struct containing one floating-point real and one integer (or bitfield), in either order, is passed in a floating-point register and an integer register, provided the floating-point real is no more than ABI_FLEN bits wide and the integer is no more than XLEN bits wide, and at least one floating-point argument register and at least one integer argument register is available. If the struct is passed in this manner, and the integer is narrower than XLEN bits, the remaining bits are unspecified. If the struct is not passed in this manner, then it is passed according to the integer calling convention. 
>
> Unions are never flattened and are always passed according to the integer calling convention. Values are returned in the same manner as a first named argument of the same type would be passed. Floating-point registers fs0-fs11 shall be preserved across procedure calls, provided they hold values no more than ABI_FLEN bits wide.

规则如下：

- 只包含一个浮点数的结构体，被当作独立的浮点实数传递。参考浮点型的传递规则。
- 包含两个浮点数的结构体，使用两个浮点寄存器来传递，不够的用栈
- 包含浮点数和整数各一个的结构体，使用一个浮点寄存器和一个整型寄存器来传递，不够的用栈

关于寄存器使用的规则和整型一样，先使用整型或者浮点寄存器，不够的在用栈

- 浮点结构体：**只有1个或2个**成员变量，且其中必须有一个是浮点类型
- 整型结构体：**超过3个**成员变量，或者没有浮点类型成员。size不超过16字节
- 大型结构体：size超过16字节(2个寄存器存不下)

解析过程：todo

#### **联合体传递规则**

直接参照整型的传递规则。注：union的大小取决于最大的那个元素

### **栈帧结构**

todo

### **相关函数**

#### pushArgs / pushArgs2

重要函数，前者先解析出传递参数的方式，设置相关标记，来决定是否要压栈以及该以何种方式压栈，然后后者来完成压栈工作。工作流程：

pushArgs部分：先初始化GP和FP=0，表示已用寄存器数量。然后遍历结点的参数列表逐个进行判断和分配。

pushArgs2：要进行两趟，第一遍对栈传递的变量进行压栈，第二遍对寄存器传递的变量进行压栈。其中只有第一趟才是真正的压栈。

Q:第二趟存在的意义是什么？以及既然是寄存器传递了，为什么还要压栈？

A:其实是为了配合后面的pop。这种push-pop组合完成了对通用寄存器的赋值工作。使得在进入函数时通用寄存器被设置为正确的值

注意参数压栈的顺序是逆序的。例如(int a, int b)这样的参数列表，我们希望a在b的上面。于是由于栈的特性，需要将b先压入栈中。

具体会在codegen生成表达式的阶段，遇到FUNCALL结点时首先被调用，并返回分配的栈传递参数的个数

#### pushStruct

将结构体的内容存到栈上。需要根据结构体的不同类型选择不同的压栈方式。具体地：

- 当遇到大结构体时候，

#### funCall

语法分析阶段的函数，负责解析函数调用。会把解析出的参数列表全部以cast结点的形式挂到这个函数调用结点上面，

语法分析

#### createBSSpace

为大结构体在栈中创建空间。同时设置了**t6**寄存器指向开辟出来的那块空间，以及设置BSDepth全局变量

#### setFloStMemsTy

设置/判断结构体的类型。会在pushArgs过程中被调用，以供后续压栈使用

#### getFloStMemsTy

获得浮点结构体成员的类型。前提：是浮点结构体。在里面会做一些检查，如果不是浮点结构体就什么也不做

#### **genExpr**

codegen阶段，遇到funcall结点时。这里负责了通过寄存器传递参数时，相应寄存器的设置。不负责栈传递相关的工作。由于之前压栈压了两趟，而第二趟就是为了此次弹栈准备的。只需将栈中内容pop到寄存器即可。

### **一些全局变量说明**

#### **BSDepth**

该次函数调用中的大结构体的深度(所占字节数)。用于辅助对大结构体压栈。在`createBSSpace`函数中被初始化为字节数。对大结构体压栈时，注意一个函数的参数中可能有多个大结构体。所以BSDepth需要在解析时不断动态更新

### **返回缓冲区RetBuffer**

在c语言视角中我们可以返回任意的类型。但在汇编视角中只能返回整数，指针，浮点数这些基础类型。对于返回其他复杂类型如结构体就需要一些操作了。处理策略大致是将要返回的数据写到一个缓冲区里，然后让a0寄存器指向那块缓冲区。也就是返回指针

todo

### **可变参数**

识别：

​		给type结构体加入了一个`IsVariadic`成员，在解析`funcParams`的时候检查最后一个参数是不是`...`。是的话设置type的`IsVariadic`标记。之后的`function`函数解析时遇到这个标记就给obj申请一块内存(buffer)用于存储剩下的可变参数。其实是一个char [64] 数组。（riscv的函数参数个数最多为8）

处理：

​			codegen在把函数的形参压入栈中后进一步检查是否有可变参数，有的话就去把那块buffer里的数据也压入当前栈中。这些数据虽然也存在于当前函数的栈中但无法像形参那样通过变量名方便地访问到，只能通过一些手段比如偏移量

## parse中的一些函数

### **declspec**

​		解析出基础类型，还有typedef、static、extern这些**属性**相关的东西

### **declarator**

​		完整地解析出一个类型，**顺便**还会解析出变量名，函数名、或者typedef的别名（被放到Ty->Name中）。使用前要先调用declspec得到基础类型，然后再调用这个函数进行进一步解析。

### **typedef**

​		在之前，declarator函数主要是负责解析类型的，也会**顺便**解析出变量名或者函数名（放到Ty->Name中）。但在加入typedef功能之后，declarator解析出来的也可能是用户自己定义的别名。这些其实可以统称为各种**标识符**。因此域中保存的不再是单单的变量名或者函数名

​		修改了`declspec`函数，使其在识别`int`, `char`等这些基础类型之前先识别typedef关键字。这里多传入了一个参数`Attr`，是用来在函数返回后指示调用者解析过程中是否发现了**特殊情况**。目前只有typedef一种可能。但遇到typedef的时候也有两种可能的情况：

- 第一次给变量取别名
- 使用已经取好的别名来给变量赋类型

​		对于第一种情况，假设输入是`typedef int * intt`，那么`declspec`会在解析到`*`的时候遇到非类型名从而退出循环(没有`*`的话就是遇到`intt`)。最终只**解析出`int`**并在`Attr`中设置一个标记。这里其实只完成了部分的声明解析，完整的声明解析在`compoundStmt`中：`declspec`返回之后马上去检查`Attr`，如果发现之前的`declspec`中声明了别名就进一步调用`parseTypedef`这个函数去解析剩下的`* intt`这个东西，他会进一步调用declarator解析剩下的这些变量标识符作为别名的类型，最后将这个别名推入当前域中。

​		对于第二种情况，假设输入为`intt a`，`compoundStmt`函数首先对`intt`执行`isTypename`，由于他之前被添加到了域中，所以会通过检查，因此被当做类型声明符使用。然后调用declspec，这里会返回别名的类型作为基础类型，然后就是像普通类型一样调用declaration进一步解析类型了。

​		`varscope -> Typedef` 这个成员只有那些被别名过的变量类型才会去设置，这带来了一个好处：`findTypedef`函数即使在当前域中找到了那个变量一样可以返回NULL，表示这只是一个普通的变量而不是被别名过的。

​		这里的任务是，对于像typedef int intt这样的语句，我们要把intt这个虚拟的变量类型添加到域中。其类型名为intt，而类型信息则完全沿用了int的。具体做法：todo

## **类型属性**

​		一些函数的参数都带有Attr这个变量，表明解析的过程中可能会遇到一些特殊情况。后续解析中有些地方会根据这个参数的情况做出一些调整

## **控制流相关**

### goto

​		放到了parse的最后阶段进行，其实是解析完function之后。因为要是遇到goto马上就进行解析会比较麻烦，他的label都不知道会在哪里，甚至可能会不存在。所以解析过程中遇到goto和label都只是把他们先收集起来，然后不管他们继续往下解析。

​		这里的处理是：遇到goto就不管三七二十一先把后面那个标识符的名字存入当前节点（用于最后的匹配），并把当前节点推入Gotos这个全局的链表。如果遇到`label:`的形式，还会额外**申请一个唯一的标识符**，这个就是最后codegen的时候跳转的目标。其他事情和goto做的也都差不多。

​		最后解析完function后，开始进行label和goto的**匹配**。每个goto在遇到自己的label后就把跳转地址设置为之前在labels中申请到的唯一标识符。如果没遇到就报错

​		codegen的时候，遇到goto节点直接无条件跳转就好，目的地已经在之前的匹配中设置好。

​	question：ND_LABEL的LHS是什么？

### for/while/if/

​		较简单，先略过，生成的时候注意结构即可

### break

​		只能用在循环中，所以解析过程放到了stmt的相应地方。每个break也会申请一个唯一标识符，用来在codegen的时候跳转。这里的技巧是当遇到break后转化为生成一个**GOTO**节点。目标用来表示循环的**结束**。这个目标标签会在codegen的时候根据循环的结构被放在合适的位置。（parse的时候其实并不知道这个标签被用来表示什么，只是在codegen的时候按照一定结构把它放到了相应的位置，才使他有了意义）

### continue

​		也是转化成GOTO语句来处理。目的是循环的开始。codegen的时候被放到了循环的开始位置，其他和break差不多

### switch、case、default

​		往Node中添加了CaseNext和DefaultCase这两个成员。

​		解析思路是：遇到switch语句之后创建一个switch节点，保存当前的switch节点到全局变量，申请一个break label(整个switch共享)，然后去switch后面的token中解析出一个Cond表达式，然后继续解析剩下的内容并存入这个节点的Then域中。

​		遇到case语句的时候先解析出case后面的val并存入节点，然后申请一个label，解析出后面的语句放入CurrentSwitch的casenext链表当中。注意case其实是一个单叉树，用不到他的RHS域

​		遇到default比case要轻松一些，不需要将其加入casenext链表也不需要为其解析出value，只要申请一个label，解析出里面的语句然后将其加入currentswitch的DefaultCase域中就好了。

​		codegen的时候，先生成Nd->Cond，也就是switch后面紧跟的那个表达式。然后遍历Nd-> casenext链表，一个个值进行比较，用beq指令拿他的值和Cond进行比较，相等就跳转到那个case的label处。比较完之后再生成**无条件跳转**的default语句(如果有的话)。之后就是逐个case生成label和里面的语句。这里的效率其实比较低，要比较很多次。实际编译器会用到跳转表之类的技术，一次就能直接跳过去

Q：break和continue语句都申请了标签，起什么作用？

​		注意到这些循环可能是**嵌套**的，而且循环内部也有很多语句要解析。所以我们需要记住当前正在处理的是哪个循环。这样当递归解析循环内部的语句的时候，如果遇到了break、continue我们可以知道该往哪里跳转。同时也可以防止在非循环体内出现这两个关键词，用来当**护卫**什么的。因为要是没有遇到循环则这些标签不会被赋值，这时候遇到break就会直接报错

​		在进入循环体的时候会申请一个break和continue的标签。

### **三元运算符**

​		添加了ND_COND和conditional函数，他会在解析出一个logOr后继续尝试匹配`?` 和`:`，从而完成Cond，Then，Else这三个成员的赋值。有个注意点是三元运算是可以递归的，因此最后解析完`:`后调用的仍然是conditional函数

## ***初始化器**

​		难点，此处的代码比较复杂

​		其实就是赋值语句中，RHS的所有的那些数据。可以用**花括号**隔开每个dimension(主要用于数组)。不隔开也行但是操作起来可能会不太方便而且也不好看。当LHS是非数组时用花括号隔开dimension就没什么必要了，不过也可以。以下几个语句都利用了初始化器来给变量初始化，并且都合法：

```c
int a = 1;
int a = {1};
int a[2][3] = {{1, 2, 3}, {4, 5, 6}};
int a[2][3] = {1, 2, 3, 4, 5, 6};
int a[2][3] = {{1}, 2, 3, 4};		// 1, 0, 0, 2, 3, 4
int a[2][3] = {{1, 2, 3}, 4};		// 1, 2, 3, 4, 0, 0
```

​		(对于第三条语句，又可看作由三个初始化器构成，对应了3对花括号)

```c
int a[2][3] = {{1}, {2}, {3}};
					   ^ warning: excess elements in array initializer
```

​		先介绍数据结构：初始化器initializer中大部分还比较好理解，children这个成员，类型是二级指针，其实就是指针数组。因为一个初始化器可以有多个子初始化器。这个主要是针对外层的花括号

​		一个desig用来给**一个**元素完成初始化，var表示元素，idx表示本次要初始化的那个元素在数组中的索引。next这个域其实是用来**表示解引用的次数**。因为**只有最外层的desig有var**，内层没有var于是每递归一次就要添加一次解引用操作。对应了访问数组内层时的操作（在`InitDesigExpr`中）

- 第一步：**新建初始化器**，`newInitializer`函数创建一个初始化器，并设置其类型。如果类型是数组还要为这个初始化器分配children，数量等于数组长度（看最**左边**的那个中括号）。然后对每个children再调用一次`newInitializer`这样就构造出了**树形**的初始化器。不过值得注意的是：这里在构建children初始化器时，传入的类型是父初始化器的**基类型**。这里蕴含的意思其实就是**子初始化器负责的维度比父初始化器要低一层**。比如	`int a[5] = {1, 2, 3, 4, 5}; `最外层的父初始化器要完成整个`arrayof(int)`的解析，而里面一层的初始化器只要解析int就好了
- 第二步：**往初始化器添加内容（解析）**。这里主要靠的是`_initializer`函数，其实是作为initializer的辅助函数，前者只负责创建框架，后者往里面添加实际的赋值语句。他会遵循推导式解析tokens。这里递归的语义是：要创建一个完整的初始化器，如果他是数组类型的，那么就创建他的每个children。如果不是那就直接给初始化器的Expr域添加一个assign节点就好。注意数组最终也都会随着递归下落到单独的一个非数组数据。
- 第三步：**将初始化列表映射到AST中的节点**。我们之前得到了抽象的初始化器这个东西，但是还不能用，得把里面的有效信息添加到AST中。**指派器**就是干这个的。他负责把assign节点添加到AST中，一个指派器负责一个数组元素的初始化。这里其实是转化成deref操作，利用数组元素的地址来对他进行初始化

### **函数说明：**

- `createLVarInit`：根据传入的初始化列表，将其转化成相应的AST，其实就是为每个元素添加assign语句。这里有个技巧是利用了逗号表达式的特性，一直往树上挂节点。desig参数就是最终我们要完成赋值的变量。
- `initDesigExpr`：生成一个指派器用来给**左边的一个元素**赋值。其实就是找出一个地址，来充当assign节点的LHS。找地址的过程中蕴含递归，每递归一次就要解引用一次
- `Initializer`：创建一个初始化器。分两步完成。先创建框架结构，再往上面添加叶子节点(assign语句)
- `_Initializer`：创建初始化器的辅助函数。接受一个已经构建好框架的初始化器，然后这个函数会往上面添加assign语句

var -> rel表示的是另外一个变量，当前变量的初始化可能会依赖于它

### **初始化器用于结构体时：**



### 省略数组长度：

​		使用了初始化器的时候，**最外层**的数组大小其实是可以省略的。这个可以根据上下文算出来。我们往初始化器中添加了一个`IsFlexible`标记，默认为打开。`newInitializer`函数在遇到这个标记，并且发现之前确实是未解析出数组大小，就会选择先不构造初始化器，直接返回空的(其实设置了Type，不算完全空，但也没啥用)。

​		解析数字和字符串时遇到这种情况会有不同的处理。字符串直接简单粗暴重新生成一个初始化器并覆盖，长度直接用token中储存的那个长度。数字稍微麻烦一些，不断调用`_initializer`这个函数并计数，算出有几个初始化器。

## **参数解析：**

用户可传入的flags：

- -c compile only。不链接只生成.o文件
- -S 只生成汇编（这里是调用RVCC）
- -cc1  也是只调用rvcc生成汇编文件
- -cc1-input   其后的参数会被赋值给BaseFile这个变量，然后传入给后续的tokennize使用
- -cc1-output  其后的参数会被赋值给OutputFile这个变量，也就是codegen时存放生成代码的文件
- -E expand，基本上只进行预处理，然后将收集到的tokens打印出来
- -###    打印链接信息
- -I  指定include路径
- -v  打印版本信息
- -W  打印警告信息（目前主要就是类型不匹配）
- ...



## **lex与yacc的使用**

程序结构：三段式

lex：使用**正则表达式**定义一套规范，然后扫描输入串并尝试匹配这些代表规范的**标记**。lex本身并不产生可执行程序，他只是把我们编写的lex规范(.i文件)转化成包含c例程yylex的c文件(lex.yy.c)。我们也可以在lex规范中定义一个main函数，这样生成的lex.yy.c再编译后就可以直接运行了。不过更多时候我们不需要可执行文件，而是把这个yylex函数进一步提供给上层的语法分析例程使用，也就是yacc产生的yyparse

匹配到后会执行标记后面的动作，不断重复这个过程。我们写的.i程序经lex编译后会产生一个叫做lex.yy.c的文件，其提供了一个**yylex()**接口，即一个按照我们的规则定义的词法分析器。可以把这个接口链接到后续的语法分析程序中以执行进一步的语法分析，或者并不去做语法分析，只是简单做些词法分析处理，这时可以在.i文件中写一个main函数，然后编译并运行lex.yy.c

yacc：位于更高级的层次。对外接口为yyparse()函数，该函数由yacc按照我们定义的规则来自动生成，我们可以放心调用它。但这个默认的yyparse函数需要我们为他提供yylex和yyerror这两个函数。由于该函数会不断调用yylex，所以要求我们在lex程序中写的写的规则，后面跟的动作块里面要有一个return的动作

协作流程：lex不断调用yylex函数，这个函数可以自己写也可以链接到-ll库使用默认的。当匹配到我们定义的规则时就会触发规则后面的动作
