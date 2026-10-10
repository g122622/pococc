# 编译统计

## 目录结构

```text
telemetry/
├── BuildJournal.hpp   # 编译事件与汇总门面
└── BuildJournal.cpp   # flock 保护的 TSV 追加和统计
```

## 模块关系与外部依赖

compiler 记录调用，application 汇总记录。依赖标准库和 Linux flock，统计文件可以由多个编译器进程并发追加。

## 容易踩的坑

- 累计任务时间包含并行重叠，不是 workflow 墙钟时间。
- 提交 distcc 不能单独证明在远端执行，fallback-allowed 的记录尤其不能作为完成数。
- 读取不完整或损坏的记录直接报错；统计应在构建结束后进行。
