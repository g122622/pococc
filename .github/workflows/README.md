# 自举工作流

## 目录结构

    workflows/
    └── bootstrap.yml   # seed、协调节点、双 worker 与证据汇总

## 模块关系与外部依赖

seed 产出 stage 0；coordinator 和两个 worker 并发运行；verify 检查 stage 1 编译与 worker 证据。连接由 src/ci/Bootstrap.py 完成。

## 容易踩的坑

- worker 等待 coordinator 的完成 artifact 后退出，异常时有 job 超时兜底。
- bootstrap result 与 worker report 包含编译数量，私钥不得进入 artifact。
- C++ modules 扫描显式关闭，所有自举编译必须经 distcc 严格执行。
