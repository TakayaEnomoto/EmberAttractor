# 最终演示与视频指南（黑洞版）

## 启动

- 双击 `Play_Lab.cmd`（1080p 独立窗口），或者用 UE 5.7 打开 `EmberAttractor.uproject` 后按 **Play**。进入后先点一下视口，让它接收输入。
- 开局会有一次爆炸，散出两类物质：橙色细点是 GPU 尘埃（默认 10 万个 sprite），亮色小方块是 CPU 碎块（默认 500 个 mesh，带碰撞）。

## 玩法一句话

鼠标移动黑洞，碰到的物质会被吞掉；按住左键会吸引并卷起周围的物质，但会消耗能量。物质会逐渐冷却，越冷越不值钱。40 秒内吃到 35 质量就赢。

## 60–90 秒作品集视频分镜（建议）

| 时间 | 画面 | 操作 / 字幕要点 |
|---|---|---|
| 0:00–0:08 | 爆炸后满屏热物质 | 字幕：一个由粒子决定胜负的黑洞小游戏（Niagara，UE 5.7） |
| 0:08–0:30 | 正常游玩一局 | 移动吞噬 → 按住左键吸引出吸积漩涡 → 能量耗尽、HUD 显示 RECHARGING → 再次吸引；画面里能看到黑洞随质量变大 |
| 0:30–0:38 | 胜利画面 CRITICAL MASS REACHED | 字幕：每个被吞的粒子都从 GPU 回读到 C++，质量反过来放大黑洞和引力 |
| 0:38–0:50 | 失败画面（故意少用吸引，等物质冷却） | 字幕：物质冷却后价值归零，能量和时间之间要做取舍 |
| 0:50–1:10 | **带 stat 叠加层的片段**：按波浪号输入 `stat Niagara`、`stat GPU`，再用 `ember.GPUCount 1000000` 加到 100 万 | 字幕：同一玩法从 10 万扩展到百万级 GPU 粒子；预算 16.6 ms / Niagara ≤ 4 ms |
| 1:10–1:25 | 按 `2` 切到 CPU 碎块，`ember.CPUCount 5000` | 字幕：少而复杂的 CPU 系统（碰撞 + 状态）与多而简单的 GPU 系统对比；结尾放性能报告里的曲线图 |

已经录好一版自动演示：仓库里的 `Video/EmberAttractor_demo.mp4`（约 70 秒，1080p60，压缩到 27 MB 以便放进 Git；高码率原片在仓库外的 `../Video/`）。内容依次是一局胜利、一局物质冷却导致的失败，然后打开 stat 叠加层，把 GPU 余烬从 100 万加到 400 万，最后对比 CPU 碎块和移植到 GPU 的碎块。重新录制：`Tools/RecordDemo.ps1 -Monitor 1 -FFmpeg <ffmpeg.exe 路径>`（游戏加 `-EmberDemo` 参数会自动演示）。想用自己实际操作的画面，也可以用 Windows 自带的 `Win + Alt + R` 或 OBS 录。

## 常用控制台命令

```text
stat Niagara
stat GPU
fx.Niagara.Debug.Hud Enabled=1 OverviewEnabled=1 OverviewLocation=450,30
ember.GPUCount 1000000
ember.CPUCount 5000
ember.Mode 3
ember.Game 0        // 实验室模式：没有黑洞计分，粒子数恒定，用来看性能
ember.Game 1        // 回到游戏
ember.TargetMass 30 // 觉得难可以调低
```

## 交作业前要自己完成的事

- [ ] 录 60–90 秒视频（至少一段带 stat 叠加层）。
- [ ] 补上自己玩/看《PixelJunk Shooter》和《Noita》的两三句观察（写进 `Documentation/Proposal.md` 或报告里）。
- [ ] 亲手玩几局，确认难度合适；`ember.TargetMass` / `ember.CoolSeconds` / `ember.RoundSeconds` 可以微调。
- [ ] 检查无误后提交并打 release tag（例如 `v1.0`）。
