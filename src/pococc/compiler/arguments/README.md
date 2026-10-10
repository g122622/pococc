# 编译参数

## 目录结构

```text
arguments/
├── InvocationAnalyzer.hpp/.cpp   # 普通对象编译能力表
└── ResponseFile.hpp/.cpp         # GNU 风格响应文件展开
```

## 模块关系与外部依赖

由 CompilationManager 调用，只依赖标准库、文件系统与 Result。分析结果只决定路由，不改变本地编译语义。

## 容易踩的坑

- 响应文件嵌套路径按编译器当前工作目录解释，限制总大小、参数数量与递归深度。
- 引号内容不是 shell 命令；不执行变量或命令替换。
- TODO: Windows/clang-cl 响应文件语法尚不支持；当前原型仅 Linux GNU 风格。
- 未验证的选项保持本地执行，不能猜测插件或附加产物行为。
