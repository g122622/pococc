# GitHub 集成

## 目录结构

    .github/
    └── workflows/   # 构建与分布式自举工作流

## 模块关系与外部依赖

workflow 调用 src/ci/Bootstrap.py 和 pococc CLI，使用标准 GitHub-hosted Linux runner。

## 容易踩的坑

- 无需预配置 Secret；使用当前 run 的 artifact 传递公钥和临时端点。
- 动作触发不修改业务项目，分布式自举在独立构建目录执行。
