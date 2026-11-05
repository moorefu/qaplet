# 17 · 标准流显式安全接入

实施起点 `2d538a1507468cb275e8b63b995f47bf58932a8e`，分支
`codex/m2-17-standard-stdio`。12/15 的本地独立合并为实际祖先。

沿用父规格 Q14/Q49/Q53/T01 已确认 seam：公开 SDK、包外纯 C Host、
真实标准流重定向与 JS 模块。系统故障夹具独立记录。使用 implement-spec 和
TDD skill 的纵向 red/green，不测试私有布局或内部调用次数。

接入与分类分开；dup 不隔离共享 flags。Host 负责协调借用标准流的替换、
共享 flags 和并发使用。未授权改变共享 flags，不能安全执行的方式拒绝。
默认创建不接管；文件 helper 的物理责任不因逻辑取消或 close 消失。

本记录随真实 slice 和验收更新；正式 Cross/Full/其他原生/64K/长测保持待验。
