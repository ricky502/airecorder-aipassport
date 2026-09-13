#pragma once

// 启动三级待机管理器。
void inspiration_power_init(void);

// 按键回调调用：恢复背光并重新计时。
void inspiration_power_note_activity(void);
