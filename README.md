# AntLang

**一块黑板，一只蚂蚁，一条规则。程序就是棋盘的初始图案。**

AntLang 1.0 是一个以[兰顿蚂蚁](https://en.wikipedia.org/wiki/Langton%27s_ant)
为执行模型的深奥语言（esolang）。源文件扩展名是 `.ant`，编译器/解释器叫
`antc`，用 C99 编写。它既能直接解释执行，也能把 `.ant` 程序翻译成独立的
C99 程序，再调用宿主编译器生成可执行文件。

```text
BOXES 声明黑板，ANT 放下蚂蚁，BOARD 画出初始黑白格。
蚂蚁在白格右转、在黑格左转，翻转当前格，然后前进一格。
```

- 语言规范：[SPEC.md](SPEC.md)
- 示例 helloworld 程序：[helloworld.ant](helloworld.ant)
- 高速公路示例：[examples/highway.ant](examples/highway.ant)

---

## 1. 快速开始

### Windows（MSVC）

```bat
build.bat
build\antc.exe run helloworld.ant
```

`build.bat` 会通过 `vswhere` 自动找到 Visual Studio 的 C++ 工具链，并在
内部调用 `vcvars64.bat`；不需要手动打开 Developer Command Prompt。

运行示例：

```text
> build\antc.exe run helloworld.ant
BOXES = (6, 3)
B W B W W W
W W W B B W
W W B B W W
ANT = (4, 0, N)
STEPS = 12
HALT = OUT
```

`helloworld.ant` 里的 `LOOP = 100000` 还没用完，蚂蚁在第 12 步就走出 6×3 棋盘，
因此 `HALT = OUT`。

### Linux / macOS / MinGW

```sh
make
./antc run helloworld.ant
```

也可以使用 CMake：

```sh
cmake -S . -B build
cmake --build build
```

### 生成 C 或原生程序

```bat
:: 生成独立的 C99 源文件
build\antc.exe emit helloworld.ant -o test.c

:: 生成可执行文件；默认输出 test.exe
build\antc.exe build helloworld.ant

:: 指定输出名和编译器
build\antc.exe build helloworld.ant -o helloworld.exe --cc cl
```

`antc build` 在 Windows 上会自动探测 MSVC（通过 `vswhere` +
`vcvars64.bat`），也支持 PATH 中的 `gcc`、`clang`、`tcc` 或 `cc`。
可以用 `--cc` 指定编译器，或设置 `CC` 环境变量。

---

## 2. 语言

### 2.1 一个完整程序(helloworld)

```ant
BOXES = (6, 3);
ANT = (2, 2, N);
OUT = true;
LOOP = 100000;

BOARD =
B B W W W W
W W B W B W
W W B B W W
```

| 指令                         | 必需  | 含义                                  |
| -------------------------- | --- | ----------------------------------- |
| `BOXES = (width, height)`  | 是   | 黑板宽高，1 基                            |
| `ANT = (x, y, E\|N\|W\|S)` | 是   | 蚂蚁初始位置和朝向                           |
| `OUT = true\|false`        | 否   | 出界是否停机；缺省 `false`                   |
| `LOOP = n`                 | 否   | 最大执行步数，`0 .. 10^18`                 |
| `BOARD =`                  | 是   | 之后是 `height` 行、每行 `width` 个 `B`/`W` |

- 关键字、`B`/`W`、方向字母都不区分大小写。
- `#` 和 `//` 开始行注释。
- 指令末尾的分号可选；`BOARD` 行末尾的分号也可选。
- 棋盘行可以写成 `B W B W`，也可以写成紧凑的 `BWBW`。
- 指令顺序任意；`BOARD` 后面直到下一条指令都是棋盘行。
- **只接受 `BOXES`**，不接受 `BOX`（`helloworld.ant` 用的就是 `BOXES`）。

详细语法和限制见 [SPEC.md](SPEC.md)。

> 该程序是 `AntLang` 官方的 helloworld 程序（暂定），第一行从第一个格子开始可以输入 n 个 B，停机轮数即为 $(n + 1) \times 4$。

### 2.2 坐标与方向

坐标是 **1 基**，原点在**左上角**：

```text
(1,1) ── x 向右 ──►
  │
  y 向下
  ▼
```

| 方向  | 含义  | 位移        |
| --- | --- | --------- |
| `E` | 右   | `(+1, 0)` |
| `N` | 上   | `(0, -1)` |
| `W` | 左   | `(-1, 0)` |
| `S` | 下   | `(0, +1)` |

右转顺序是 `E → S → W → N → E`；左转顺序是 `E → N → W → S → E`。

所以 `ANT = (3, 3, E)` 表示第 3 行第 3 列（从左上角数），面朝右。

### 2.3 单步语义

每一步：

1. 读取蚂蚁当前格的颜色；
2. **白格右转，黑格左转**；
3. 翻转当前格（白变黑，黑变白）；
4. 沿新方向前进一格；
5. 步数加一；
6. 如果 `OUT = true` 且蚂蚁出界，停机；
7. 否则进入下一步。

关键细节：

- 先转、再翻、再走；初始格子的颜色参与本步转向。
- 出界那一步已经翻过格子，也计入 `STEPS`。
- `LOOP` 在每一步开始前检查，`STEPS` 恰好等于执行步数。
- 第 `LOOP` 步同时出界时，`HALT = OUT`（出界在该步内部发生）。
- `LOOP = 0` 表示一步都不走，`STEPS = 0`。

### 2.4 停机规则

- 至少要有 `OUT` 或 `LOOP` 之一。
- `OUT` 缺省或为 `false` 时，棋盘外是**无限白格**，蚂蚁可以一直走；
  此时必须有 `LOOP`。
- `OUT = true` 时初始蚂蚁必须在 `BOXES` 内。
- `OUT = false` 时初始蚂蚁可以在棋盘外（棋盘外是白格）。
- 只有 `OUT = true` 而没有 `LOOP` 是合法的，但蚂蚁可能永远不出界，
  程序就可能永远运行；需要保证停机时请提供 `LOOP`。
- `OUT = false` 且没有 `LOOP` 会被静态拒绝：程序没有任何停机条件。

### 2.5 关于“104”和“高速公路”

`LOOP` 没有写死 104 或 10^4；它由程序自己给出，最大 10^18。`test.ant`
用 100，只是示例。兰顿蚂蚁从全白棋盘出发，通常在一万步左右进入周期性
的“高速公路”（highway）；想观察这个现象可以运行：

```bat
build\antc.exe run examples\highway.ant --view bbox
```

该示例使用 `LOOP = 12000` 和 `OUT = false`。

---

## 3. 输出

默认输出由五部分组成：

```text
BOXES = (6, 6)
<height 行棋盘>
ANT = (x, y, D)
STEPS = n
HALT = OUT|LOOP
```

- `BOXES` 是程序声明的尺寸，不是视图尺寸。
- 棋盘行从 `y = 1`（顶部）排到 `y = height`（底部），格子之间一个空格。
- `HALT = OUT`：蚂蚁出界停机。
- `HALT = LOOP`：跑满 `LOOP` 步停机。

### 3.1 `--view bbox`

默认只显示声明的 `BOXES` 矩形。`OUT = false` 时蚂蚁可能走到棋盘外，
此时用：

```bat
build\antc.exe run examples\highway.ant --view bbox
```

输出会增加一行：

```text
VIEW = (x0, y0, width, height)
```

视图是“声明的棋盘 + 最终所有黑格 + 最终蚂蚁位置”的最小包围盒，坐标
可能为 0 或负数。显示区域超过 4,000,000 格时会拒绝输出，并提示改用
`--view board`。

### 3.2 `--trace`

逐步打印每一步，再打印最终棋盘：

```text
STEP 1: (3,3) B, E -> N, W, (3,2,N)
STEP 2: (3,2) W, N -> E, B, (4,2,E)
...
```

字段含义：步数、移动前位置、移动前颜色、旧方向、新方向、翻转后的
颜色、移动后位置与新方向。

### 3.3 `--no-board`

只输出 `ANT`、`STEPS`、`HALT` 三行，适合脚本处理。

---

## 4. 命令行

```text
antc run   <file.ant> [--trace] [--view board|bbox] [--no-board]
antc check <file.ant>
antc emit  <file.ant> [-o <file.c>]
antc build <file.ant> [-o <program>] [--cc <compiler>] [--keep-c]
antc <file.ant>              # 等价于 antc run
antc help | --help
antc version | --version
```

| 命令/选项          | 说明                                     |
| -------------- | -------------------------------------- |
| `run`          | 解释执行                                   |
| `check`        | 只做词法、语法和静态检查                           |
| `emit`         | 输出独立 C99 源码；默认写 stdout，`-o` 写文件        |
| `build`        | 生成 C 并调用宿主编译器；默认输出 `<名字>.exe`（Windows） |
| `--trace`      | 逐步跟踪                                   |
| `--view board` | 默认视图：只显示声明的 `BOXES`                    |
| `--view bbox`  | 扩展视图：包含棋盘外的黑格和蚂蚁                       |
| `--no-board`   | 不打印棋盘，只打印状态                            |
| `--cc`         | 指定编译器，例如 `cl`、`gcc`、`C:\...\cl.exe`    |
| `--keep-c`     | 保留 `build` 生成的中间 C 文件                  |

退出状态：

| 状态  | 含义                |
| --- | ----------------- |
| `0` | 成功                |
| `1` | 源文件、I/O、代码生成或运行错误 |
| `2` | 命令行用法错误           |

---

## 5. 构建 `antc`

### Windows + MSVC

```bat
build.bat
```

脚本会：

1. 如果 `cl.exe` 已在 PATH 中，直接编译；
2. 否则用 `vswhere.exe` 找 Visual Studio；
3. 调用 `vcvars64.bat` 设置环境；
4. 用 `/W4 /O2 /std:c11` 编译 `src\*.c` 到 `build\antc.exe`。

### Make

```sh
make            # 生成 ./antc
make clean
```

### CMake

```sh
cmake -S . -B build
cmake --build build --config Release
```

### 编译器要求

- C99 或更新；
- 只使用标准库（`stdio`、`stdlib`、`string`、`stdint`、`stddef` 等）；
- 生成的 C 程序不依赖 `antc` 或任何运行时文件。

---

## 6. 示例

| 文件                                             | 演示内容                            |
| ---------------------------------------------- | ------------------------------- |
| [`test.ant`](test.ant)                         | 根目录示例，`OUT = true`，第 8 步出界      |
| [`examples/blank5.ant`](examples/blank5.ant)   | 全白棋盘 + `LOOP`                   |
| [`examples/checker.ant`](examples/checker.ant) | 棋盘格图案，快速出界                      |
| [`examples/highway.ant`](examples/highway.ant) | 12000 步，观察高速公路，配合 `--view bbox` |

运行：

```bat
build\antc.exe run examples\highway.ant --view bbox
build\antc.exe run examples\highway.ant --trace
build\antc.exe emit examples\highway.ant -o highway.c
build\antc.exe build examples\highway.ant -o highway.exe
highway.exe --view bbox
```

---

## 7. 测试

测试使用一个独立编写的 Python 参考模型（`tests/reference.py`）作为
oracle，与 C 实现做差分比较。

```sh
# 解释器 + 静态错误 + 40 个随机程序
python tests/run_tests.py

# 更多随机程序
python tests/run_tests.py --fuzz 200

# 同时编译每个用例的生成 C，并比较原生程序输出
python tests/run_tests.py --native --fuzz 50 --native-fuzz 10
```

Windows 上如果 `build\antc.exe` 还不存在，先运行 `build.bat`。测试不依赖
`vcvars` 环境：`antc build` 会自动探测 MSVC。

测试覆盖：

- 每个示例的默认视图、`bbox` 视图、`--no-board`、`--trace`；
- 全部静态错误（缺规则、越界、行宽、颜色、方向、重复、未知指令、
  `BOX` 拼写、负数 `LOOP`、溢出等）；
- 随机棋盘、随机蚂蚁位置、`OUT` 真/假、`LOOP = 0`；
- 解释器与生成 C 的输出逐字比较。

---

## 8. 设计决策

这些是 1.0 明确固定下来的选择，也是实现和测试的依据：

1. **指令名是 `BOXES`，不是 `BOX`。** 现有 `test.ant` 使用 `BOXES`；
   写 `BOX` 会得到明确错误提示。这样不会出现两个近义指令。
2. **坐标 1 基、原点左上、y 向下。** 这与棋盘文本从上到下的书写顺序
   一致，`S` 自然对应 `y + 1`。
3. **白格右转、黑格左转。** 这是最常见的兰顿蚂蚁版本；翻转后前进。
4. **`OUT = false` 表示无限白平面。** 不是环绕，也不是“出界即停”；
   棋盘外默认白色，蚂蚁可以继续走。
5. **`OUT = false` 必须有 `LOOP`。** 否则程序没有任何停机条件，静态
   拒绝比让 `run` 永久挂起更友好。
6. **`OUT = true` 且无 `LOOP` 可以合法非终止。** 蚂蚁可能永远不出界；
   这是语言允许的，按 Ctrl+C 中断即可。
7. **`LOOP` 计的是单步数，不是完整扫描轮数。** `LOOP = 0` 合法。
8. **`OUT` 优先于 `LOOP`。** 第 `LOOP` 步同时出界时，`HALT = OUT`。
9. **默认视图是声明的 `BOXES`。** 棋盘外的变化需要显式 `--view bbox`；
   这样默认输出尺寸稳定、易于 diff。
10. **`BOXES` 的顺序是 `(width, height)`**，即先列后行。

### 限制

| 项目          | 限制            |
| ----------- | ------------- |
| `BOXES` 宽、高 | 1 .. 4096     |
| 棋盘总格数       | 1,000,000     |
| `ANT` 坐标    | -10^9 .. 10^9 |
| `LOOP`      | 0 .. 10^18    |
| 源文件         | 64 MiB        |
| bbox 输出     | 4,000,000 格   |

---

## 9. 目录结构

```text
.
├── build.bat              # MSVC 构建脚本（自动找 vcvars）
├── Makefile               # gcc/clang 构建
├── CMakeLists.txt
├── SPEC.md                # 语言规范
├── README.md
├── helloworld.ant         # 根目录实例 helloworld 程序
├── src/
│   ├── antc.h
│   ├── main.c             # CLI
│   ├── parse.c            # 解析与静态检查
│   ├── sim.c              # 解释器、黑格哈希集合、输出
│   ├── codegen.c          # .ant -> C99，以及 build 驱动
│   └── util.c             # 文件、内存、诊断
├── examples/
│   ├── blank5.ant
│   ├── checker.ant
│   └── highway.ant
└── tests/
    ├── reference.py       # 独立 Python 参考模型
    ├── run_tests.py       # 差分测试
    ├── cases/             # 合法程序
    └── invalid/           # 静态错误程序
```

---

## 10. 为什么这算一门语言？

程序不是指令序列，而是一块初始棋盘；蚂蚁是唯一的“指令指针”，黑白格
是它的“纸带”。规则只有两条（白右、黑左），但初始图案可以编码任意
有限布局。兰顿蚂蚁在无界平面上具有通用计算能力，因此 AntLang 不是
单纯的可视化玩具，而是一个极简的计算模型。

同时它也是一个很好的教学示例：解析器、静态检查、哈希集合、逐步执行、
输出渲染、C 代码生成和差分测试，都能在一个几千行 C 项目里看到。

---

## 11. 参考

- Chris Langton, *Studying artificial life with cellular automata*, 1986.
- Wikipedia: [Langton's ant](https://en.wikipedia.org/wiki/Langton%27s_ant)
- 语言规范：[SPEC.md](SPEC.md)
- 示例：[test.ant](test.ant)
