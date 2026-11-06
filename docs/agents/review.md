# 独立审查材料

审查前固定 base/head，用 `scripts/review_packet.py` 生成各轴材料；派发者只提供对应 packet 目录及该轴问题。报告收齐后再聚合。packet 只隔离已标注材料，不能代替审查者判断内容。

## 生成

context manifest 的 schema_version 为1，contexts为数组。每项提供仓库相对POSIX文件路径path、audience（common/standards/spec/aggregate）。规范、需求等参考源标注kind=reference，审查结论标注kind=report（默认）。默认source为git，读取固定head的blob；历史独立报告等忽略文件可显式使用source=file，读取并快照当前原始字节。每个路径只能分类一次。

```sh
python3 scripts/review_packet.py build --base BASE --head HEAD \
  --axis standards --contexts .scratch/FEATURE/review-context.json \
  --output-dir out/reviews/RUN/standards
python3 scripts/review_packet.py build --base BASE --head HEAD \
  --axis spec --contexts .scratch/FEATURE/review-context.json \
  --output-dir out/reviews/RUN/spec
```

common和本轴的快照进入packet；另一轴审查结论和aggregate材料也从diff中排除。reference文件的实际变更仍保留在两轴diff中，使规范与需求修改获得完整审查。每个packet记录解析后的commit SHA、merge-base、完整commit list、diff与文件摘要。输出必须使用仓库out下的新目录。当前复盘的分类示例为[review-context.json](../../.scratch/qaplet-retro/review-context.json)。

## 验证和派发

生命周期差异逐项核对证明边界：managed closed 证明托管端点结束，jobs handoff 证明 owner 交付完成，physical completion/join 证明执行者退出，native quiescence 证明必要记录与操作资源回收。每项零计量/释放断言须对应其实际屏障；逻辑完成后的保留容量符合合同，不能为测试提前释放。核对至少一个执行者仍未退出的负向场景，确认失败/保留没有被签发为清理成功。

时间字段、缺失元数据和继承执行的机械组合由合同测试覆盖；跨模块的完成屏障与所有权判断由审查者核对。静态结论与实际执行成绩分别记录。

重型推送前先固定 head，完成相关本地检查和两轴审查。审查发现需修改时先生成新 head 的材料；构建使用固定输入快照，不在运行中的供应源树继续收口。已验原件保留各自身份，不能改为新头成绩。

`python3 scripts/review_packet.py verify --packet out/reviews/RUN/standards` 重核材料摘要、成员清单和audience边界。先验证，再向fresh reviewer提供该目录。上下文以packet中的快照为准；当前工作目录文件可能已变化。聚合报告留给两轴返回之后的整合步骤。

此工具检查文件完整性与显式分类，不认证外部作者、不推断自然语言中隐藏的另一轴结论；分类和公共材料的选择由派发者审核。原code-review技能要求的 Standards/Spec 范围、Fowler基线及每轴报告长度保持不变。
