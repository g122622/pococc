# 文档

## 目录结构

```text
docs/
└── USAGE.md   # 构建、节点启动、项目接入和运行限制
```

## 模块关系与外部依赖

使用指南对应 application 和 compiler 的公开入口，依赖 Linux 工具链与 distcc。

## 容易踩的坑

- 区分提交给 distcc 和确认在远端执行；示例先运行严格 doctor。
- 多 worker 同机测试不提供跨机器加速比。
