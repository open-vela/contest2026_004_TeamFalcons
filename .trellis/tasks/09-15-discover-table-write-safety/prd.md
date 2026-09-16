# 点表落盘数据安全

## Goal

阻止空表与非法字段写进候选/已确认表：scan 后 apply 清空已确认表、重扫 dump 丢候选编辑、unit 未校验写坏 JSON、scale 精度丢失、非有限浮点丢阈值

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
