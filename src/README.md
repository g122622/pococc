# 源码

## 目录结构

```text
src/
├── main.cpp       # 统一参数入口和退出码
├── pococc/        # 核心子系统
└── examples/      # 独立示例源码
```

## 模块关系与外部依赖

入口调用 application 门面，所有实现与头文件都在本目录。测试程序位于根目录 tests。依赖 C++20 与 Linux 系统接口。

## 容易踩的坑

- 不用 shell 拼接编译命令，保留每个 argv 的边界。
- 运行诊断写到 stderr，避免污染编译器 stdout。
