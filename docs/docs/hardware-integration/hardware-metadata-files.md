---
title: K380 硬件元数据约束
---

本 K380 fork 不维护 ZMK hardware metadata 文件，也不维护由 schema 生成的硬件事实。

K380 的硬件事实只由根仓库 PRD 和根 `docs/` 下的派生契约文档维护。ZMK 仓库不得重新引入
hardware metadata schema、由 schema 生成的 metadata TypeScript 文件，或独立的管脚/矩阵事实来源文档。

当 K380 硬件行为需要变化时，先更新根 PRD；确认后再从 PRD 派生 ZMK 实现规格。
