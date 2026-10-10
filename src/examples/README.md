# 示例源码

## 目录结构

```text
examples/
└── hello/   # 三个编译单元的独立 CMake/Ninja 示例
```

## 模块关系与外部依赖

示例不依赖 pococc 代码，通过 CMake compiler launcher 接入 pococc-cc。

## 容易踩的坑

- 首次 configure 时指定 launcher；构建时用 pococc run 提供节点环境。
