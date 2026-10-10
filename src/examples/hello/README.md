# 编译示例

## 目录结构

```text
hello/
├── CMakeLists.txt   # 独立示例目标
├── Main.cpp         # 输出计算结果
└── arithmetic/      # 可独立编译的运算函数
```

## 模块关系与外部依赖

Main 调用 arithmetic，三个编译单元经 launcher 分发，链接仍本地执行。只依赖标准库。

## 容易踩的坑

- 示例源码在 src，构建产物放 build-hello；输出应为 42。
