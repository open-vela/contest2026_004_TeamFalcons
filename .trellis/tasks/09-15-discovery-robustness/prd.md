# 探查功能健壮性加固

## Goal

修正 vgdiscover/vgscan/vgpoint 探查链路中会静默毁数据、绕过总线互斥、丢失发现结果的缺陷，让空表覆盖、字段写坏、截断无提示这类问题在协议层可见

## Requirements

- TBD

## Acceptance Criteria

- [ ] TBD

## Notes

- Keep `prd.md` focused on requirements, constraints, and acceptance criteria.
- Lightweight tasks can remain PRD-only.
- For complex tasks, add `design.md` for technical design and `implement.md` for execution planning before `task.py start`.
