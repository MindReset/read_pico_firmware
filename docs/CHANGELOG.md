# 版本变更 / Changelog

按版本简述对用户可见的变化；使用方法见 [README](../README.zh-CN.md)。
User-visible changes by release; see [README](../README.md) for usage.

## 0.3.3-rc95 / 2026-10-16

- 修复无变化差分或异常返回后像素驱动可能保持开启的问题：结束时统一释放驱动，短暂 I2C 失败有界重试，避免静止白底持续受驱动；不增加刷新或改变波形、电压与画质。
  Release pixel drive after unchanged differentials or failed output, retrying transient I2C errors without extra scans or changes to waveforms, voltage settings or image quality.

## 0.3.3-rc94 / 2026-10-16

- 阅读手动与周期全刷恢复 rc90 的原表 GC16 目标页输出，取消纯文字全刷前的独立擦白和后续文字补刷；保留普通翻页预绘、插图与水波纹设置。
  Restore rc90's original-table GC16 target-frame output for manual and periodic reader refresh, removing the separate white clear and text pass while retaining ordinary prefetch, images and ripple settings.
- 接入 PR #34：修正竖排阅读辅助线的位置和虚线节奏，补齐横线类符号旋转，并按字体原生全角步进对齐章节数字编号。
  Integrate PR #34 for vertical reading-guide placement and dash phase, dash-like punctuation rotation and chapter-number alignment using native fullwidth metrics.
- 接入 PR #32：RTC 轨迹记录开机各阶段耗时；首帧后停止开机标记，普通回首页不挤占记录。保留后台封面缓存加载。
  Integrate PR #32 boot-phase RTC timing, stopping boot marks after the first frame and retaining background cover loading.
- 接入 PR #33：设置 → 升级和恢复 → 屏幕修复，手动执行面板黑白恢复与当前页面整屏重建；结果提示随最终画面一次显示，输出失败保留恢复路径。正常阅读波形、底栏边界和分段滑动不变。
  Integrate PR #33 manual panel repair in Settings → Upgrade and recovery, restoring the complete current page with its final notice once and retaining output-failure recovery. Normal reading waveforms, navigation bounds and discrete scrolling remain unchanged.

## 0.3.3-rc93 / 2026-10-15

- 优先优化连续翻页：下一页预绘与当前页推屏重叠，连按复用已准备内容，未完成的目标接着绘制。保留正式 rc92 的阅读波形、插图、水波纹和周期清残影规则。
  Prioritize rapid turns with output-overlapped prefetch and partial-target reuse; retain rc92 reading waveforms, images, ripple and periodic cleanup.
- 首页和书架封面使用有界后台任务，优先复用 TF 成品缓存；过期结果按代次丢弃，书架整组显示。普通亚克力书架题头与内容合并为同一轮校准灰阶输出，固定底栏保持。
  Prepare covers in bounded background jobs, reuse processed TF caches, reject stale results and reveal shelf groups. Ordinary acrylic headers/content share one calibrated gray scan with fixed navigation.
- 拼贴锁屏复用小型书库索引，长章节行缓冲按实际文字增长，减少重复扫描与内存峰值；保留锁屏画质和低内存回退。
  Reuse a small collage catalogue and grow line scratch on demand, retaining artwork quality and low-memory fallbacks.
- 首页继续阅读整块可点，并增加封面反馈；「返回主页」按开书来源回首页或原书架分类页。资料卡头像后台准备，不再阻挡首页与设置操作。
  Make the entire continue card actionable with cover feedback, return to the opening's Home/shelf origin and prepare avatars without blocking page input.
- 改进 S3 RTC 计时，已联网每小时校准，离线空闲每六小时短连已保存 Wi-Fi 对时；休眠和其他网络操作可取消后台对时。
  Improve RTC timekeeping with hourly online and six-hour idle offline calibration; background sync yields to sleep and other network operations.
- 增大底栏触控范围并在首次接触时响应，优化普通主页切换、底栏边界和状态签名显示。三种主页模式的设置列表恢复分段滑动，取消此前跟手与惯性方案。
  Expand root-tab targets and respond on initial contact, refine ordinary transitions, navigation boundaries and status signatures. Restore discrete settings swipes in all main modes, removing follow/inertial scrolling.
- 底部上滑固定打开阅读工具栏，取消按键必须映射工具栏的限制；首行缩进控件增加字号与间距，返回字体设置统一黑底白字。
  Keep bottom-edge Tools independent of key mappings, enlarge and separate indent controls and use the matching dark return button.
- 官网网页刷机、TF、联网 OTA 与 Release 附件来自同一次正式构建；保留旧版本固定下载地址。
  Publish one build across web, TF, OTA and Release assets while retaining immutable older downloads.

## 0.3.3-rc92 / 2026-10-14

- 首页新增 Moon、Kehai、Cat、Dali、Asterisk、小屋时钟与最近阅读书夹，保留 IKEA PS1995；长按选择样式，单击打开对应时间页或书架。阅读记录新增月历、每日目标与“已闭环”。
  Add selectable clocks and the recent-reading folder, matching time pages, monthly records and daily goals.
- 猫眼与 Kehai 秒针在普通、快刷、水波纹模式使用短局部刷新；Kehai 保留一圈浅色轨迹，避免动态区域被主页水波纹改写为较重刷新。修复书夹预览、边线与返回键反馈。
  Preserve short local clock motion in all main modes, a bounded Kehai trail and exact feedback borders.
- `books` 下的子目录可作为书架分类，从书架题头下拉选择；切页往返保留分类与当前页。书架布局位置及封面显示同步调整。
  Select shelf categories from books subdirectories and retain category/page positions across navigation.
- 设置列表使用跟手黑白滚动并加入可打断的减速惯性；普通与水波纹停止后补回灰阶，快刷保持黑白。滚动复用有界缓存，低内存回退原绘制。
  Add interruptible inertial settings scrolling with bounded binary caches and gray settling where needed.
- 阅读字重改为胶囊减/加，每次微调 1/16 像素覆盖率，旧配置保留实际粗细；自动全刷后的纯文字沿普通文字成像路径，减轻第一页偏淡。
  Refine weight by 1/16-pixel coverage steps, preserve legacy thickness and align automatic text cleanup with ordinary text formation.
- 合入 PR #27、#28、#31 原实现：竖排阅读、翻页器完整报告帧学习与连接订阅修复、RTC 崩溃记录及锁屏图标统一。
  Include the original PR #27/#28/#31 implementations for vertical reading, Bluetooth reports/link reliability, crash records and lock icons.
- 关机菜单采用滑动确认与圆形重启/取消；头像成品缓存到 TF 卡，票根使用系统字体与抗锯齿，首页边线及标题可读性优化。
  Add slide-to-off, round restart/cancel controls, cached avatars and clearer system-font ticket/Home rendering.
- 官网刷机、TF、联网 OTA 与 GitHub Release 来自同一次产品构建，保留历史固定下载地址。
  Publish one product build for USB, TF, OTA and GitHub Releases while retaining immutable older downloads.

## 0.3.3-rc91 / 2026-10-12

- 合入 PR #24、#25，支持 EPUB 内嵌 TTF/OpenType 字体，修复开书与关闭书内字体的栈溢出，减少大型章节索引和多字体缓存占用。
  Include PR #24/#25 for embedded EPUB faces, stack safety and bounded index/glyph memory.
- 阅读新增五档字重和抗锯齿开关；重排字体与阅读设置，中键长按默认返回首页，底部上滑始终打开工具栏。
  Add five stroke weights and optional antialiasing, paired reading controls, default Home on middle hold and bottom-edge Tools.
- 系统设置重新分组，字号并入系统字体，选择字体可确认同步阅读；100–200%保持单行，阅读工具与子面板适配系统字号。
  Regroup Settings, confirm reader-font sync and fit large system sizes in Settings and reader panels.
- 修复边距滑杆、居中章节题头的两行对齐和间距，字体丢失稳定回退；水波纹插图页使用一次灰阶更新。
  Fix margin stops, two-line heading alignment/spacing and missing-face fallback; present illustrated ripple pages once in gray.
- 书架、首页续读与拼贴复用TF成品封面；书架整页准备好后一起显示，修复偶发封面漏刷，保留冷缓存有界准备和低内存回退。
  Reuse prepared TF covers, reveal whole shelf pages and retain retries and bounded cold-cache work.
- 壁纸锁屏缓存最终16级灰阶抖动画布；拼贴优化缓存准备，主动锁屏先清白显示圆锁再生成完整画面，保留画质。
  Cache full-quality wallpaper locks and streamline collage preparation with immediate white/lock feedback.
- 主页和阅读水波纹采用8条带、11,090µs目标节拍，保留完整37相与固定底栏；普通纯文字翻页减轻短暂压黑。
  Match main/reader ripple launch timing while retaining gray phases and navigation; soften transient darkening on ordinary text turns.
- 设置只绘制可见行并复用头像，入口箭头独立于字体；阅读状态栏按分钟更新时间。
  Cull Settings rows, reuse avatar pixels, draw independent chevrons and update the reading clock each minute.
- 修复 WiFi 断联重连和确认遗忘；TF容量使用真实挂载分区并重算过期FAT32提示；票根长统计保持边界。
  Fix WiFi reconnect/forget, partition allocation counts and ticket statistic bounds.
- 合入官方 a2a63a6 的睡眠/关机外设与引脚低功耗处理，保留密码、按键唤醒和阅读恢复；功耗以实机为准。
  Port official low-power peripheral/pin parking, preserving PIN, key wake and reader recovery; power draw requires hardware measurement.
- 在线刷机、TF与OTA使用同一次产品构建，Bin、完整USB包和SHA-256发布至GitHub Release；原固定版本地址保持。
  Publish one verified product build for USB, TF and OTA with immutable older URLs and Release checksums.

## 官网说明书 / 2026-10-10

- 上线 21 章、135 项操作、30 幅界面图解的详细说明书和 69 页 PDF；新增「问说明书」，支持关键词、口语问句与模糊检索，直接定位到操作步骤。保留旧说明书链接及 PDF 下载地址，固件仍为 rc90。
  Publish the illustrated manual and searchable task finder; retain existing manual/PDF links and the rc90 firmware.

## 0.3.3-rc90 / 2026-10-09

- 固件名称统一为 kiikoread；资料卡移除署名与电量，姓名和编辑垂直居中。水波纹主页使用与普通模式相同的灰阶画面。
  Rebrand the firmware as kiikoread, simplify the profile card and center its name/edit action; ripple and normal main pages share gray artwork.
- 新增 kiikoread.com 主站联网升级，失败回退 GitHub；保留原刷机与更新入口。正式二进制统一保存到 GitHub Releases，网页与 OTA 使用经过 SHA-256 验证的同一构建。
  Add primary-site OTA with GitHub fallback, preserve existing flashing/update entry points and store official binaries in Releases with verified website/OTA mirrors.
- 合入 PR #22：缺失系统字体不再每 750ms 反复加载内建字体并触发全刷，字体回到卡内后仍能自动加载。
  Merge PR #22: missing system fonts no longer repeatedly reload the built-in or trigger full refresh; retries still pick up restored files.
- 合入 PR #23：只读确认卡内为不支持的文件系统时提供格式化提示，需再次确认清空全部数据；初始化、供电与读写错误不误提示格式化。
  Merge PR #23: offer formatting for read-only confirmed unsupported media, with explicit second-tap data-loss confirmation; avoid misleading hints for initialization, power and I/O failures.


- 官网品牌改为 kiikoread，采用纯白界面；增加可切换放大的界面展示、功能介绍、统一版本的刷机/下载/日期/日志，以及同源在线和 PDF 使用说明。保留原发布包、历史版本与升级网址。
  Rebrand the bright-white website as kiikoread, with selectable enlarged screen previews, features, consistent release flashing/download/date/notes and shared HTML/PDF documentation. Preserve published binaries, archives and upgrade URLs.
- GitHub 刷机首页跳转至 kiikoread.com，保留版本选择与页面位置；原固件下载、使用说明和联网更新地址继续可用。
  Redirect the GitHub flasher homepage to kiikoread.com with the selected version and page anchor preserved; existing firmware, manual and OTA URLs remain available.

- 应用确认的较小列表封面120×156，保持原中心点、右侧书名/作者/进度及局部弹动，等比例填充并居中裁切。
  Apply the approved 120×156 list covers, preserving their center, text/progress positions and local bounce with aspect fill and centered cropping.

- 列表封面点击改为小幅上弹后回位，复用现有封面，仅刷新移动范围；快刷黑白、普通/水波纹灰阶，无额外反馈框。
  List covers bounce and settle using existing artwork and movement-only local differentials; fast retains BW and ordinary/water grays, without an added frame.

- 密码锁屏缓存增加样式与壁纸身份校验，切换后深睡唤醒/冷启动不再误用之前的拼贴图；自动休眠与手动锁屏继续使用当前选择。旧缓存自动重建，密码和配置不受影响。
  PIN artwork caches now check the selected style and wallpaper identity, preventing stale collage reuse on cold/deep wake. Auto/manual lock keep the current choice; old artwork is regenerated without changing credentials or settings.

- 修复 TF 卡检测通信失败被误判为拔卡的问题；高速初始化失败先等待重试，再降至 20/10 MHz，`ESP_ERR_INVALID_STATE` 不再直接终止重试。初始化错误不自动建议格式化；保留真实拔卡失效、显式重挂及 USB 独占保护。
  TF detection I/O failures no longer masquerade as removal. Failed high-speed initialization settles and retries before 20/10 MHz fallback, including `ESP_ERR_INVALID_STATE`. Init failures do not suggest formatting; true removal, explicit remount and USB ownership guards remain.

- 新增 D 版书架拼贴锁屏：原比例真实封面、15° 倾斜、圆角与16级灰阶；资料卡名字及大小分明的书籍数量使用导入系统字体原生绘制，TF缓存、有界工作内存与低内存回退。
- 移除半透明书袋和书脊样式，旧设置及备份回退亚克力；保留票根、壁纸、两种原书架样式及原睡眠恢复逻辑。
- 浅色拼贴封面增加沿真实圆角边缘的细灰描边，缓存自动重建；深色封面保持原样。
- 新增可选四位数字锁屏密码：开启确认、验证原密码后修改/关闭，真实锁屏模糊背景、导入系统字体、圆内按下反馈及局部黑白快刷；浅睡/深睡/冷启动验证后恢复页面，取消保持锁定。凭据使用加盐摘要，普通配置恢复不覆盖；低内存、触摸读错和多指取消不会绕过验证。
- 密码键盘输入仅局部更新圆内反馈及密码圆点，不处理背景或字形，优化数字、删除与取消的响应速度。
- 新增每页四本的列表书架：统一尺寸的等比裁切封面、作者、阅读进度、收藏标记及双行长标题，支持翻页、重启和配置恢复。
- 拼贴封面新增柔和投影，白色文字描边减细；锁屏图像使用16级灰阶抖动表现256级输入亮度，保持有界工作内存。
- 密码顶部锁图标改为平滑圆角，减轻背景模糊；密码入场一次显示完整画面，不补刷第二段背景，低内存与冷启动直接显示最终密码页。
- 列表书架外框和分隔线加深至双像素，常用按钮、设置卡片边框同步加强，快刷与水波纹模式保持可读。列表封面统一120×156像素，等比放大后居中裁切。
- 首行缩进新增1像素微调（-20～+20，默认0）、数值点击复位及重启/配置备份恢复，正文标题与对齐块保持原位。

Add a D-layout library collage lock with aspect-preserved real covers, native imported system typography, SD caching, bounded workspaces and safe memory fallback. Pale covers gain a fine rounded gray outline. An optional four-digit PIN verifies wake/cold starts, retains the saved page, confirms setup and requires the old PIN for changes. Cached frosted lock art and native system glyphs use local feedback; faults cannot bypass authentication. Salted credentials are excluded from ordinary backups. Retired pocket/spine settings migrate to acrylic.

- 文件页的存储卡卡片在“卡在但文件系统读不了”时改为显示“需要格式化 / 格式化可能可以解决此问题”，点一次询问、再点一次才格式化，点别处取消。此前这种情况与“卡没插”显示同一句话，把用户指向了错误的排查方向。
  The storage card on the files page now reads "needs formatting / formatting may fix this" when a card is present but its filesystem cannot be read, with one tap to ask and a second to format (a tap elsewhere cancels). It used to show the same line as an absent card, which pointed at the wrong problem.
- 加快主页水波纹：使用与阅读相同的一拍一带调度，将24带83拍改为16带52拍，保留12ms目标节拍、完整37相、原灰阶波形、底栏范围与异常恢复。软件目标由996ms缩至624ms，实际速度与残影需实屏确认；阅读翻页、刷新模式默认值不变。
  Speed up main-screen ripple with reader-style compact launches: 16 bands/52 ticks instead of 24 bands/83 ticks, retaining the 12ms target, all 37 phases, gray waveform, navigation bounds and recovery. The software target drops from 996ms to 624ms; hardware speed and ghosting require device checks. Reader turns and refresh-mode defaults are unchanged.

## 0.3.3-rc89

- 移除微信读书登录、在线书架、同步、下载功能与相关源码、测试及网页预览。文件管理直接显示 WiFi 传输、热点传输、USB 传输三个入口，加深传输卡片边框。
- 联网升级前释放闲置封面缓存，低内存时安全提示；屏幕扫描与固件擦写互斥，进度条只追加新增黑段及百分比，避免频繁整页清理。保留有界重试、SHA256 校验、取消等待与双槽回退。
- USB 完整包、TF 升级包和 OTA 清单统一更新，基础包分区与用户数据地址保持兼容。

## 0.3.3-rc88 · 2026-10-08

- 优化启动、EPUB 元数据与封面加载的栈占用；快刷只缓存黑白封面，普通/水波纹只缓存灰阶封面，切模式释放旧格式，低内存时安全回退。
- 异常开书保护：开书被断电或异常重启打断后，停止自动重试该书，进入可操作页面；已记录书名时提供返回首页及确认删除，不自动删除文件。
- 深睡前保存书籍、阅读位置、全屏和主页面状态，唤醒后恢复；恢复记录只消费一次，避免失败后反复尝试。
- 中键长按可自定义，包含返回首页；三键短按或中键长按至少保留一个阅读工具栏入口，旧配置/恢复配置缺少时补为中键短按。配置备份 v11 兼容旧版本。
- 新开书默认全屏；翻页收起工具栏并回到全屏，保留旧布局阅读锚点，避免重排跳读。中间双击独立切换全屏。
- 下拉快捷面板及四个圆形按钮加深框线，三种主页模式统一灰阶；票根统一系统字体，浅睡解锁恢复此前阅读字体。
- 首页继续阅读书名使用系统字体，阅读书名与章节标题使用阅读字体，复用当前字体按原 40/32/48 像素直接生成字形；不增加内建字库或同时加载两份字体。
- 在线刷机、TF 升级 Bin 与联网 OTA 同源发布，保留双槽回退与已有数据；下架 rc87，保留原发布的 rc85/rc86 可选。

Startup and EPUB/cover stack use are reduced, with one bounded cover format per main refresh mode and safe low-memory fallback. Interrupted book opening stops automatic retries and offers Home or confirmed deletion for a recorded book. Deep sleep checkpoints the reader and consumes resume once. Custom middle-hold actions, persistent Tools-entry repair and compatible v11 backups keep controls accessible. Page turns close Tools and restore full screen without skipping the old anchor. Quick controls use clear gray borders in all modes; tickets use the system font. Home titles reuse the active system face at 40px, reader titles the active reading face at 32/48px, without extra embedded faces or concurrent font loads. USB, TF and OTA share a compatible rollback build. rc87 is withdrawn; original rc85/rc86 remain available.

## 0.3.3-rc87 · 2026-10-08 · 已下架 / Withdrawn

- 在线刷机页新增正式版本选择：默认最新版本，也可刷入原发布的 rc86 或 rc85；显示对应发布时间并提供该版 TF 升级 Bin。
  The web flasher offers the latest release plus original rc86/rc85 builds, showing each publication time and matching TF image.
- 主页刷新模式：系统设置可选普通、快刷、水波纹，默认普通。普通及水波纹保留灰阶；快刷使用黑白文字与封面，亚克力书架采用细腻规则的棋盘格点阵。
- 四个主页面切换保留底栏图标与文字，选中小横条单独局部刷新。修复锁屏返回后底栏消失，并限定内容刷新边界；手动全刷、首次进入、解锁和异常恢复保留完整刷新。
- 底栏图标、圆形返回键与书架管理/导入按钮增加点击反馈；修复底栏放大动效周围的矩形白边。三种主页模式统一保留原灰底，按真实变化缩小推屏范围，释放、滑出、多指或切页时复位。
- 新增顶部下滑快捷菜单：从顶部边缘下滑可连接最近保存的 WiFi / 再次断开、切换蓝牙、全刷与锁屏。向上滑、点击外部或返回键收起；传输忙碌时保留网络，蓝牙沿用现有内存保护。
- 阅读按键可自定义，提供两套推荐：设置1为左上一页 / 中阅读工具栏 / 右下一页；设置2为左首页 / 中切换全屏 / 右阅读工具栏。中键长按固定全刷，中间双击切换全屏不随设置变化；单击左右各半或上1/3、下2/3翻页。配置随重启及保存恢复保留。
- 阅读水波纹替换为快速动画；灰阶相位保持完整，插图与手动/周期全刷仍使用原路径。优化主页面水波纹条带与快刷退出书架的同向擦白；缓存最多九张黑白封面，切页与锁屏不回放旧画面。
- 改善阅读题头与首页书名中补充汉字放大的锯齿，保持原字宽与排版。
- 在线刷机、TF 升级 Bin 和联网 OTA 来自同一次产品配置编译；核对双槽基础包、升级校验值和版本，保留已有配置、阅读数据与 TF 卡内容。联网更新确认页显示简短日志。

Main screens offer ordinary, fast and water refresh modes. Ordinary/water retain grays; fast uses monochrome artwork and fine checkerboard acrylic. Retained navigation has local marker and press feedback, with actual-change bounds protecting its background and labels. A top-edge quick layer controls saved WiFi, Bluetooth, refresh and lock. Reader keys are configurable with two presets, fixed middle-hold refresh and independent center double-tap full screen. Fast reader ripple, bounded cover caching and interpolated supplemental Han glyphs improve the display. USB, TF and OTA share one product build with compatible slots, rollback and release notes.

## 0.3.3-rc86 · 2026-10-08

- 新输入法：九宫格只输入中文拼音；全键盘支持中英文与大小写切换，数字和符号使用独立页面。文件/文件夹改名、资料卡名称、状态栏签名、书架搜索、书名编辑及 WiFi 密码共用这套界面。
- 连续拼音支持 488 个离线常用词组与选词后联想；同音字支持跨批次翻页，九宫格「更多」可选择其他读音。拼音未确认时保留输入，确认后再保存；内建黑体补齐 GB2312 的 6763 个汉字。
- 补齐常亮输入光标：轻点文字或左右箭头调整位置，在光标处插入或删除；长内容跟随光标滚动，完整处理 UTF-8 字符、密码显示/隐藏及大系统字号。
- 输入按键按下加深并轻微下沉，松开或移开恢复，只刷新该键。删除键长按约半秒后连续删除，先删待选拼音，再删光标前的文字，松手、移开、多指或离页立即停止。
- 优化打字刷新：连续输入使用局部快刷；联想词栏累计变化六次后，停手约 700 毫秒只做一次局部灰阶整理，减轻残影。打字和长按删除期间延后整理，静止画面不反复刷新。
- WiFi / 热点传书网页新增状态栏签名设置，可保存或清空，重启后保留。
- 修复部分 EPUB 兼容性，避免局部对齐样式影响整篇正文；采用 PR9 图文混排核心，补齐图片缓存与异常回退，优化图片缩放。网页刷机增加设备筛选和中文连接提示，保留擦除确认。
- 在线刷机、TF 升级 Bin 与联网 OTA 同源发布，更新日志同步显示在设备更新确认页；继续保留双槽回退、已有配置与阅读数据。

The shared keyboard now supports Pinyin-only T9, bilingual QWERTY, case switching and separate number/symbol panels. Continuous Pinyin uses 488 offline phrases, continuations and pageable homophones; the embedded black face covers 6,763 GB2312 Han characters. Steady carets, insertion-point editing, local key feedback and held backspace are available across native input scenes. Typing stays on fast local updates; six candidate-strip changes followed by a 700 ms touch-free pause trigger one bounded gray settle. AP/STA web transfer can save or clear the persistent status signature. EPUB alignment, image resampling and PR9 image flow are improved. USB, TF and OTA share one build, with release notes, rollback and user-data preservation.

## 2026-10-07 · mindreset

- 合并 PR12：封面和插图采用中心对齐的双线性缩放，改善细线与图中文字的显示；修正映射运算及损坏 PNG 的缓冲区释放。
- 合并 PR13：网页刷机默认保留用户数据，擦除需主动勾选；仅列出 Espressif USB 串口，安装对话框改为中文，并避免翻译触发反复处理。保留当前 rc85 固件包与升级版本。

PR12 adopts center-aligned bilinear image resampling with safe coordinate arithmetic and PNG cleanup. PR13 defaults to preserving user data, filters Espressif USB ports and translates installation dialogs without repeated mutation handling. Published rc85 binaries remain unchanged.

## 0.3.3-rc85 · 2026-10-07

- 优化阅读文字翻页：纯文字页之间使用 CrossMux / FreeInk 文字波形与灰阶差分，减少重复擦白及新字等待；普通阅读与全屏均生效。涉及插图或图文混排时保留原灰阶路径，水波纹、周期及手动全刷继续有效。在“阅读设置 → 翻页效果”选择“默认效果”体验。
- 图文兼容修复：补齐 SVG 包装图片及较长 JPEG 头部的尺寸识别，缺失插图记录时继续绘制正文；保留章节分页、标点规则、阅读线和未知图片回退。
- 蓝牙扫描隐藏没有广播名称的设备；后续扫描响应补齐名称后可显示，点击对应正确设备。
- 翻页器适配：扩展原生按键支持，保留手动映射优先及键盘修饰键保护。
- 在线刷机、TF 升级 Bin 与联网 OTA 同源发布，更新日志同步显示在 rc84 及以后固件的更新确认页；保留兼容双槽布局与用户数据。

Text-only turns use the CrossMux / FreeInk waveform and grayscale differentials in normal and full-screen reading. Image/mixed-page transitions, water effects and periodic/manual cleanup retain their existing paths. This release also includes SVG/JPEG image-flow fixes, named-device Bluetooth filtering with later name updates, and Bluetooth pager compatibility with manual-mapping precedence. USB, TF and OTA share one build, preserving the dual-slot layout and user data. Devices running rc84 or later display the release notes before online installation.

## 0.3.3-rc84 · 2026-10-07

- 图文混排：小图与正文在同一页排版，按比例缩放，多图接排。
- 滑动翻页：默认支持左右滑动，兼容上下翻页模式。
- 自动休眠：可选 1 分钟、5 分钟、10 分钟或关闭。
- 蓝牙翻页器优化：学习映射显示识别到的实际按键。
- 图标 UI 优化：升级、BOOT 和阅读设置使用独立图标。
- 文件管理排版优化：统一操作和分页按钮，增强文字与线框可读性。

联网更新确认页新增简短更新日志，提供“稍后 / 开始更新”。从 rc84 开始，后续更新可在设备上查看日志；旧固件升级本次版本前可在在线刷机页查看。完整刷机、TF 升级和联网 OTA 使用同一编译，保留兼容的双槽基础包、现有设置与阅读数据。

The release adds inline images, swipe page turns, configurable idle lock, clearer learned Bluetooth keys, distinct UI icons and improved file-management layouts. From rc84 onward, the online update offer displays short release notes before installation. USB, TF-card and online updates share one build and retain the compatible dual-slot layout and user data.

## 2026-10-07 · Codex

- rc83 本地测试：删除“全局刷新模式”测试项及实验刷新引擎，旧 NVS / TF 备份不再开启该模式，保留标准刷新及阅读全刷设置。晃动翻页改用新加速度采样区分左右：左晃上一页、右晃下一页；一次横向动作触发，过滤倾斜、其他方向及回弹，触摸和工具栏期间禁用，离页恢复传感器睡眠。方向与灵敏度已通过模拟轨迹回归，仍需实机试用。

- rc83 本地测试：采纳 PR #7 的比例缩放、页内多图及后续正文接排；保留有界尺寸探测，补齐未知尺寸回退与每张图失败提示，按布局代次更新缓存，章节加载失败保留原章及进度。混排页保持灰阶并沿用普通翻页刷新，纯图页保留完整灰阶刷新。采纳 PR #8 的蓝牙学习按键值显示；已覆盖多图、空间边界、重排、隐藏图片、内存失败与按键结果回归，真实图片观感及翻页器待实机验证。

- rc83 本地测试：合并 PR #5/#6，USB 停止失败时保留存储卡所有权并允许重试，恢复串口 PHY；补齐 CI 的 USB 配置。管理书架移除旧菜单入口，修复“下一页”误进示例菜单，统一筛选、操作和分页按钮，保留跨页勾选。EPUB 同文件内的正文章节从新页开始，标题与开头正文同页；过滤目录链接、简介及版权信息，普通小节继续接排。本地版本号保持 rc83，固件尚未发布到在线刷机页。
## 2026-10-07 · mindreset

- 蓝牙翻页器的按键映射改为显示实际学到的键：以前只写“自定义按键”，现在直接给出该键的原始字节（如 0x88），字节下标或报告号非零时一并补上，两个绑定不会再看起来一样。
## 2026-10-06 · mindreset

- 正文图片改为图文混排：插图不再占据整页，而是作为页面流里的一个块放在正文之间，图下方接着排后续正文；只有当前页放不下时才翻页。图片按栏宽等比缩放、不超过一页高、不放大，水平居中。
- 排版仍然不依赖图片解码器：尺寸由调用方通过回调提供并在本章缓存；优先使用已缓存的有界头部尺寸，无缓存及回调时保留整页显示。

## 2026-10-07 · mindreset

- 蓝牙扫描列表不再列出没有广播名的设备：那些行只能显示地址，认不出是什么设备，地址还会把信号强度挤掉。
- 翻页器适配：新增按键协议的原生支持：0x01 上一页、0x02 下一页，无需学习。

## 2026-10-06 · Codex

- 固件 0.3.3-rc83：
  - 书架封面抽出与取消改用局部灰阶差分，只驱动变化的像素，减少点击时的黑闪。
  - 设置主页、蓝牙及扫描页滑动更早响应，一次至少移动三行；各设置列表仅刷新内容区，取消滑动时按次数强制黑白清屏，到边界不重复刷新。
  - 状态栏时间、电量百分比增至 28 像素，个性签名增至 26 像素；按实际字形与图标中心对齐，长签名避开两侧信息。
  - 修复蓝牙翻页器及扫描页面叠出设置资料卡。
  - 修复文件管理首次打开图片时重启；支持直接打开根目录或其他目录中的图片，并浏览同目录图片，保留灰阶层次。
  - WiFi 与热点传书网页新增“添加文件到当前目录”，支持根目录、多级目录、任意格式与空文件；保留原名称，同名覆盖需确认，失败保留原文件。
  - 系统字号上限提高到 200%，随重启及配置备份保留；合并屏幕 PMIC 的 VCOM 门控更新，重新开启 VCOM 后等待 2 毫秒再扫描。
  - 完整刷机包、TF 升级 Bin 和联网 OTA 使用同一固件与版本；基础包保留双槽升级、首次启动确认及异常回退能力。已通过产品配置编译和宿主回归，实际刷新观感与硬件门控效果仍需实机验证。

- 固件 0.3.3-rc82：联网检查发现新版本后直接显示“稍后 / 开始更新”，修复任务收尾后按钮仍显示“取消联网更新”的问题；下载进度至少间隔 5 秒且变化达到 5% 才更新，仅刷新百分比与进度条区域，不累计整页清残影。更新期间关闭 WiFi 省电并在结束后恢复；连接等待延长，短暂断网允许最多三次重连续传，核对续传范围、文件长度与完整 SHA256，失败保留原版本。阅读页返回键、书名与收藏图标统一视觉中心线，内建字体按实际字形边界居中。基础包、TF 升级 Bin 和联网清单同源发布，保留双槽回退及已有数据；已发布的版本下载地址继续保留，避免旧清单缓存导致更新文件失效。已通过产品配置编译、宿主回归和原生渲染核对，真实无线环境与墨水屏观感待验收。
- 在线刷机页在当前版本下显示更新时间，统一使用北京时间；后续打包自动更新记录。
- 固件 0.3.3-rc81（联网测试版）：修复 WiFi/热点选择页按下反馈误触发整页刷新、取消点击而无法进入传输页的问题；已保存 WiFi 时直接启动传输，未配网时进入网络选择。状态栏 WiFi、蓝牙图标使用与电池相同的黑色，稍微缩小并靠近电量百分比。所有圆形返回键外圈统一加深。
- 书架按下书籍向上抽出，取消触摸恢复；页码两侧新增对齐的上一页/下一页箭头，首末页相应箭头置灰。蓝牙翻页器上/左键上一页，下/右键下一页；支持已学习的原始按键，同一报告只执行一次。书架翻页复用局部刷新，各书架样式均生效。基础包与升级包仍使用兼容双槽分区，提供官网 HTTPS 联网更新与 TF 升级 Bin；实机联网升级及观感待用户验收。
- WiFi 连接页采用加深双层卡片边框、清晰分隔线和较大字号；已连接时提供断开按钮，断开保留凭据，传输中拒绝断开。阅读设置新增翻页模式：上下模式以正文高度三等分，上区上一页、中区切换全屏、下区下一页；普通及全屏阅读均生效，支持上下滑动翻页。左右模式保持原有规则，偏好随重启及配置备份恢复。
- 固件 0.3.3-rc80（本地验证版）：蓝牙翻页器移到 WiFi 下一行，状态栏加入 Lucide 蓝牙图标并支持与 WiFi 同时显示；按键学习主动响应，顶部显示「识别成功」，清理旧输入并修正带报告 ID 的键盘释放识别。网络分配前串行暂停蓝牙，防止同时初始化；保留内存门限，检查蓝牙任务创建结果，失败回收资源，网络退出后恢复蓝牙。修正 BOOT 行点击范围。
- 阅读「刷新设置」改为「阅读设置」，集中放置全屏沉浸、电源键翻页、关闭书内图片及长按圆圈键全刷；删除系统设置「阅读操作」。长按圆圈键默认回首页，开关开启后整屏全刷，并加入配置备份。普通阅读题头增加圆形返回键，返回进入书籍前的页面。首行缩进按实际字体字宽计算，并校正段首引号、括号的可见边缘。
- 固件 0.3.3-rc79（本地验证版）：合并 PR #3 的渐进式 JPEG 封面及比例裁剪、PR #4 的 BLE 蓝牙翻页器和大缓冲 PSRAM 分配。蓝牙设置、扫描及按键学习页按当前设计统一，修正滑动、返回与停止扫描；WiFi/微读/OTA 工作时暂停蓝牙以减少内部内存竞争。
- 设置新增“升级和恢复”分组：系统升级、保存与恢复、BOOT 刷机独立成行；文件管理移除 BOOT 按钮。系统升级支持已有 TF 卡升级和 HTTPS 联网 OTA：验证官方源、设备型号、版本、应用槽、完整大小、SHA256 与镜像，只写空闲槽，完整验证后才切换启动。官网发布工具同时生成全量基础包、TF Bin 与 OTA 清单，发布前验证基础包能力与分区兼容性；本地页面增加 Bin 下载，尚未发布新版在线固件。
- 微读同域完整响应复用 HTTPS 会话，清除旧请求鉴权头与回调；延长建连与无数据等待，临时断网允许重连和有界重试，避免逐章重复握手。真实账号下载和 BLE 设备配对仍需实机验证。
- 电量显示统一采用 PMU 软件油量表，优先读取带 CRC 与有效位的轻量快照，无有效读数显示未知，资料卡和状态栏使用相同取整。此修订不改变 PMU 电压或保护参数，实际电量误差仍需充放电记录验证。新增蓝牙开关加入配置备份，兼容旧备份。

- 固件 0.3.3-rc78（本地验证版）：微读返回键明确返回文件管理，书籍详情进入独立二级页，不再显示书架同步与退出登录；下载文件使用微信书架书名，不再追加书号。封面始终下载，与正文插图开关分离；封面获取失败会提示重试。微信书架增加跨页多选、全选本页及逐本批量下载，一次最多选择 1024 本，取消或离页会停止队列，已完成文件保留。保留 rc77 的目录字体、可读性与图片空白占位修复。已通过本机编译及主机回归，真实账号下载与实机操作仍待验证，未上传在线刷机页。
- 固件 0.3.3-rc77（本地验证版）：阅读目录全部使用当前系统字体，返回正文恢复独立的阅读字体；加深目录卡片及按钮边框，增大上页、下页、快翻十页和百分比跳转文字。排版跳过插图前后的空白段落、全角及零宽占位，减少多余空行和空白页，关闭图片时正文正常接续，保留原文定位。保留 rc76 的传书与微读功能；未上传在线刷机页。
- 固件 0.3.3-rc75（本地验证版）：补齐已确认的首页排版，题头与书架使用相同位置、字号和绘制方式，横线统一在同一高度；继续阅读区整体下移 40 像素，点击范围与封面局部刷新同步调整。近七天图表保持原布局。
- 固件 0.3.3-rc74（本地验证版）：采用 PR #2，从文件管理打开并保存进度的 TXT/EPUB 自动加入书架；管理页新增「移出书架」，保留文件及阅读进度，再次阅读自动上架。批量按钮改为两行三列；移出状态随 TF 卡配置备份恢复，兼容旧备份。保留 rc73 的 Lucide 图标。已通过产品配置编译与主机回归测试；实机显示和操作待验收。

## 2026-10-05 · DeepSeek Harness

- 统一采用 Lucide 图标：底栏、WiFi、返回键、阅读工具栏、设置、文件管理与电源弹窗使用同一套线条样式；已添加书签显示轮廓加勾。同步图标生成工具与许可声明。
- 书架收录读过的书：从文件管理打开并保存进度的 TXT/EPUB 可以位于 TF 卡任意位置，返回书架时自动补入；已删除或文件大小已变的记录不补入。管理页新增「移出书架」，只隐藏条目，文件和进度保留，再次打开重新上架；批量按钮改为两行三列。

## 2026-10-05 · Codex

- 固件 0.3.3-rc73（本地验证版）：完整采用 PR #1 的 Lucide 图标方案，尺寸、线宽和灰度与确认预览一致。已通过产品配置编译与主机测试；实机显示待验收。
- 固件 0.3.3-rc72：修复部分 EPUB 兼容性。
- 固件 0.3.3-rc71（本地验证版）：关闭书内图片时，排版跳过插图块并让前后正文接续，不再保留插图页或显示「此插图暂无法显示」占位提示；纯插图章节仍沿用跳过逻辑。尚待实机验证，未上传在线刷机页。
- 固件 0.3.3-rc70（本地验证版）：全屏阅读底部细进度条上移 4 像素，并同步调整独立刷新区域，避免翻页时正文刷新影响进度线。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc69：修复票根锁屏遇到持续按键中断时可能停在静态画面的问题；浅睡期间定时补查电源键，睡眠连续失败时返回可操作页面。票根仅使用已有封面与元数据缓存，避免锁屏时解析大 EPUB；书架多级目录扫描改为固定栈占用，文件管理递归目录操作的路径与复制缓冲移到外部内存，降低重启风险。配置恢复先校验再替换旧阅读记录，并核对写回的 WiFi 凭据。阅读「刷新设置」增大文字并加深选项边框，字体列表长名称按卡片宽度截断。已通过产品配置构建和主机回归测试；仍待实机验证长时间锁屏、书架进入与配置恢复。详细说明见 [rc69 更新说明](RELEASE_NOTES_0.3.3-rc69.md)。
- 固件 0.3.3-rc68：将「封面与书脊」样式标为测试版、非正式版本；在线刷机固件汇总 rc59–rc68 的双槽升级、书架收藏、排版、壁纸、备份及 WiFi 内存优化。详细功能、验证范围与注意事项见 [rc68 更新说明](RELEASE_NOTES_0.3.3-rc68.md)。
- 固件 0.3.3-rc67（本地验证版）：书架封面和书脊收藏标记统一为黑色，深色背景增加贴合图标轮廓的白边。优化 WiFi 在线时打开图书的内存余量：开书前释放可重建的封面与预渲染缓存、按需启动预渲染，并清理损坏 EPUB 索引的临时分配；书架打开失败会显示原因。尚待实机联网上书验收，未上传在线刷机页。
- 固件 0.3.3-rc66（本地验证版）：TF 卡壁纸按原比例居中裁切铺满锁屏，横图和方图也能在解码器限制内显示；配置备份增加已保存的 WiFi 名称与密码，兼容旧备份，并提示妥善保管 TF 卡。设置页卡片边框与分隔线加深、加粗；所有书架样式都将收藏书排在前面，再按当前排序规则排列。含 rc65 的收藏标记白底修复。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc65（本地验证版）：书架封面和书脊左上角的收藏标记去除纯白矩形底块，按封面局部灰度使用深色或浅色实心标记；阅读页标题旁的收藏标记保持原有深色样式。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc64（本地验证版）：保存书签后图标完整填黑；配置备份新增每本书的阅读进度、阅读时长、书签、收藏和自定义书名，继续保存设备资料与状态栏签名，并兼容旧备份。阅读首行缩进不再叠加书籍原文自带的段首空格；修复书架管理页返回键、筛选点击区域与未选书时的操作提示。尚待实机视觉及 TF 卡恢复验收，未上传在线刷机页。
- 固件 0.3.3-rc63（本地验证版）：新设备的阅读行距默认改为滑块中档 130%，保留已有手动设置；普通阅读正文上缘适当下移、下缘延伸到页脚进度区前，全屏正文上缘增加留白、底部细线移近屏幕下沿，正文与刷新范围同步调整，减少页面下方留空。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc62（本地验证版）：资料卡头像与圆角遮罩使用同一边界，并自动裁去明显的白色留边；阅读工具栏的书签在保存后显示可辨认的选中态，收藏图标的 V 形尾部完整填充，图标线宽与深浅统一。尚待实机视觉验收，未上传在线刷机页。
- 固件 0.3.3-rc61（本地验证版）：调整热点与同 WiFi 传书服务的启动顺序和内存占用，修复 `ESP_ERR_HTTPD_TASK` 导致网页无法启动；减少上传时的碎片写入、关闭传书期间的 WiFi 省电，并延长弱信号重连窗口。书架支持扫描 books 内的多级目录、收藏书籍和书脊样式；统一阅读字号与最近阅读排序，增加关闭书内图片选项。改善插画刷新、壁纸填充与灰阶、正文两侧对齐和全屏排版；修复设备名称与状态栏签名保存失败时无提示的问题，并对齐设置资料卡中的电量与编辑文字。尚待实机验收，未上传在线刷机页。

## 2026-10-04 · Codex

- 固件 0.3.3-rc60（本地 OTA 验证版）：作为 TF 卡升级包验证双槽切换、首次启动确认和异常自动回退；功能与 rc59 相同，版本号递增用于确认设备内升级成功。
- 固件 0.3.3-rc59（本地 OTA 基础版）：16 MB Flash 改为双应用分区，保留原 NVS 与内部存储地址；文件管理新增 TF 卡本地升级，可校验并安装根目录的 `Pico-update.bin`。升级只写空闲槽，完整性校验通过后才切换；新槽首次启动失败会自动回退。首次启用仍需通过电脑完整刷入本基础版。
- 固件 0.3.3-rc58：修复部分 EPUB 打开时设备自动重启的兼容问题；目录解析的大块临时数据改用外部内存。实机已验证可正常打开并翻页。同步发布 rc51–rc57 的目录、阅读、界面、锁屏及配置备份改进。
- 固件 0.3.3-rc57（仅本地构建）：设置主页恢复原行高并支持上下滑动，固定状态栏、标题和底栏；资料卡头像居中裁切到圆角遮罩，状态栏签名按屏幕中心对齐，输入法显示中文、英文小写和英文大写切换状态。首页模块边框、分隔线和书架横线加深；无封面书籍的第三种模板换成上深下浅的新样式并重建旧缓存。新增的设备资料、显示、睡眠和阅读选项随 TF 卡配置备份与恢复。
- 固件 0.3.3-rc56（仅本地构建）：壁纸与票根锁屏均在浅睡十分钟后转深睡；电源菜单可选彻底关机或先浅后深。修复全屏阅读底部进度线；设置新增首页强刷、设备名称与头像、状态栏签名；BOOT 入口移到文件管理。
- 固件 0.3.3-rc55（仅本地构建）：首页、书架、设置与文件管理切换改用完整 16 级灰阶表，每 3 次切换做一次同屏灰阶整理，保留每 12 次完整清残影；文件管理的卡片边框、列表分隔线与辅助文字加深、加粗或放大。阅读线样式与上下位置移入独立二级菜单；全屏翻页将最底部进度线单独差分，减少反复闪动。补齐 rc54 阅读线位置调节的固件实现。
- 固件 0.3.3-rc54（仅本地构建）：阅读线加深至双像素，可在字体设置中上下微调位置并随配置备份；全屏阅读扩展正文区域，屏幕底部增加细的全书进度线；字体设置同级卡片统一高度。刷新设置新增每 30 页全刷与「不强制全刷」，保留手动整屏全刷；旧配置仍可恢复。
- 固件 0.3.3-rc53（仅本地构建）：兼容部分 EPUB 对普通 ZIP 条目设置的扩展标志，修复文件被误判为不支持、无法打开的问题；仍核对压缩方式、文件结构和 CRC，并完成正文读取验证。
- 固件 0.3.3-rc52（仅本地构建）：TXT 和缺少有效封面的 EPUB 自动生成三种确定性灰阶封面，书架、首页与阅读票根共用同一封面缓存；书内有效封面继续优先显示。延续 rc51 的目录快翻、百分比跳转和 EPUB 目录识别修复。
- 固件 0.3.3-rc51（仅本地构建）：目录每次可快翻 10 页，新增百分比跳转；目录标题使用更清晰的固定字号。EPUB 的 NCX 目录缺项时补入 NAV 中可用的章节与同文件锚点，重复项保留 NCX 名称，并重建旧目录缓存。修复阅读排版在默认字间距下调节边距时变化不明显的问题。
- 固件 0.3.3-rc50：阅读正文插图直接使用解码后的灰阶，不再叠加深色增益与系统对比度；翻到插图页时用完整灰阶波形从白底呈现，保留更多暗部层次。系统对比度的首次默认值改为最低档 100%，已有手动保存的设置继续保留。
- 固件 0.3.3-rc49（并入 rc50）：图片页改用独立的完整灰阶波形，先清除上一画面，再从白底呈现原始灰阶，改善插画与照片的层次。阅读统计中的书签支持多选删除；文件管理长按文件或文件夹可重命名、复制、移动或删除，轻点保持打开；书架“管理”与“导入”入口改为当前设计风格。
- 固件 0.3.3-rc48（并入 rc50）：阅读正文按整字列居中，改善右侧留白明显更宽的问题；排版设置新增首行缩进 0／1／2／3 字，默认 2 字，并纳入 TF 卡配置备份与恢复。中文右标点与前字同排，避免标点单独出现在下一行开头或悬挂到右侧留白。

## 2026-10-03 · Codex

- 固件 0.3.3-rc47：修复全屏阅读时水波纹翻页误回退为默认效果；退出全屏后整页重绘章节与进度信息。系统设置新增 TF 卡配置备份与恢复，涵盖字体、排版、显示和阅读操作；恢复前校验文件并对缺失的外部资源安全回退。EPUB 清单与章节按实际数量分配内存，扩展到 8,192 项，并优化大书目录索引与打开速度；修复 1,546 章 EPUB 被旧的 1,024 章上限拒绝的问题。
- 固件 0.3.3-rc46：阅读线改为按字号与行距定位在相邻两行文字之间，并加深虚线和点线，避免线条贴近下方文字；网页刷机页自动读取刷机包版本，修复页面仍显示 rc42 的问题。
- 固件 0.3.3-rc45：修复首次无 TF 卡开机后插卡不会重新探测的问题；挂载完成时首页、书架和文件管理重新读取目录。拔出已挂载的卡仍须安全关闭旧文件并显式重挂，避免误写。
- 固件 0.3.3-rc44（预览构建，未刷机）：字体设置主面板只保留字号、阅读字体、晃动翻页和阅读线；边距、行距、段距和字间距集中到「排版设置」二级面板，保留原有数值与拖动行为。
- 固件 0.3.3-rc43（预览构建，未刷机）：阅读正文轻点中央切换全屏并重新排版；系统设置新增“阅读操作”，可选电源键短按下一页、长按锁屏及全屏隐藏状态栏。字体设置按参考图重排，正文默认首行缩进两字，字间距默认居中并可向两侧微调；新增段距和点击轮换的无／虚线／点线阅读线。
- 固件 0.3.3-rc42：设置页点击 Pico 设备卡片可进入固件升级页，选择“进入 BOOT 模式”后保存 TF 卡写入并重启到 ROM 下载模式；USB 读卡结束时重新将内部 USB PHY 交给串口/JTAG，供电脑再次识别和刷机。网页刷机指南补充 BOOT 模式入口与使用时机。
- 固件 0.3.3-rc41：阅读页底部只显示章节名，移除自动补上的章号；首页续读作者右侧不再显示章号。续读书名优先按当前字体实际宽度缩小字号完整显示，保持原布局位置。
- 固件 0.3.3-rc40：内建系统字体改为稍厚的思源黑体 Medium（500 字重），保持与阅读字体分离；锁屏壁纸选图增加整图预览、上下滑动换图和设定确认；书架样式预览改用无书名的通用封面；TF 卡挂载后按需建立 books、fonts、pictures；目录跳转后立即更新底部章节与百分比；EPUB 大图改用更省内存的渐进缩放解码，改善扉页插图显示。
- 固件 0.3.3-rc39：EPUB 首次打开目录时检查正文中的“第一章 / 第1章”等独立标题；识别到完整章节序列时优先据此生成目录并精确跳转，识别不完整时只校正对应标题并保留书内目录。过滤普通段落提及的章节字样，结果写入索引，后续直接读取。
- 固件 0.3.3-rc38：参照 CrossMux 将 EPUB 正文 spine 与 NCX/NAV 目录分开，保留同一 XHTML 中多个章节的标题与锚点；点击目录跳转到对应正文位置，目录当前项按条目高亮。旧 EPUB 目录索引自动重建；书前作者、出版社资料仍从目录中排除。
- 固件 0.3.3-rc37：首页继续阅读在最近打开的书籍变化后立即更新，复用封面缓存而不重复探测 TF 卡；从首页打开书籍直接显示阅读页，长按中键退出时返回首页。EPUB 目录优先采用书内导航，缺少导航时过滤书前作者、出版社、版权等资料页，阅读进度仍沿用原章节位置。
- 固件 0.3.3-rc36：阅读页刷新设置新增可持久化的「默认效果 / 水波纹效果」，升级后仍默认沿用原刷新规则。水波纹采用 16 条带、37 相错相 GL16，单次差分计算，沿逻辑翻页方向执行；周期全刷仍覆盖整屏。动画异常时重建前后帧基准，缺少相位或低内存时回退普通 GL16。原先移除方向动画的产品决定改为「默认关闭、用户可选」，阅读字体设置面板保持原样。
- 固件 0.3.3-rc35：放大状态栏时间、WiFi、电量百分比与电池图标，并统一垂直对齐；文件管理根目录的文件夹名称与图标居中对齐。
- 固件 0.3.3-rc34：实机验证发现网页上传书籍后写入阅读进度时会触发重启；传输服务任务栈改回内部内存，并在同 WiFi 模式下继续验证上传与目录操作。
- 固件 0.3.3-rc33：热点与同 WiFi 传书共用新版 TF 卡文件管理网页；新增手机锁屏壁纸裁剪，支持拖动位置与缩放，按 Pico 684 × 1216 像素导出 JPG、上传到 pictures 后自动设为壁纸锁屏。目录重命名后同步当前传书目录，防止本次会话的后续上传写向旧路径。

## 2026-10-02 · Codex

- 固件 0.3.3-rc32：热点传书网页改为 TF 卡目录浏览与管理，可逐层查看根目录、分页、新建文件夹、重命名、复制和删除文件或文件夹；书籍、字体、图片分别上传到 books、fonts、pictures。移除网页的图片对比等多余入口，热点限制单设备接入，传输时暂停设备页面交互与定时刷新。文件改名或删除时同步维护阅读进度、字体、壁纸和自选目录设置。
- 固件 0.3.3-rc31：USB 读卡页重排连接状态、存放目录与安全弹出步骤；修复连续写入时缓冲区覆盖和扇区偏移丢失，写卡失败会向电脑返回错误，USB 模式使用更保守的 TF 时钟。
- 固件 0.3.3-rc30：阅读设置刷新图标换成提供的图样，与其他工具图标统一中心和灰度；阅读页放大书名、章节及进度文字；状态栏文字固定字号并与 WiFi、电池对齐；首页统一细节左边距、七日标题基线并加深图表分隔线。
- 固件 0.3.3-rc29：首页按布局编辑器保存的画面重排，移除继续阅读白色卡片与深色按钮底框；封面、书名、作者、章节、进度和七日阅读标题对齐新坐标，柱形图继续显示真实阅读记录。
- 固件 0.3.3-rc28：首页改为继续阅读卡片与近七天阅读时长圆角柱形图，读取已有的每日阅读记录；首页只加载当前阅读封面，避免多本封面逐一刷新。状态栏的时间、WiFi、电量百分比和电池统一垂直中心线。
- 固件 0.3.3-rc27：WiFi/热点传书启动前释放字形缓存；成功的 WiFi 配置请求正常返回，重复文件等请求错误不再把无线服务显示为故障。上传中禁止切页打断文件。USB 读卡禁用主机弹出时的自动二次挂载，先断开主机并排空写入再退出；TF 卡卸载失败不再交给电脑接管。USB 传输缓冲区由 512 字节增至 8192 字节，入口先显示准备页。
- 固件 0.3.3-rc26：阅读目录改由独立页面模块绘制和处理点击，不再复用书架列表与旧局部翻页逻辑。目录使用新版浅灰底与白色圆角章节卡片；章节标题合并换行和异常空白、限制为单行并在超宽时截断，每页 7 节。增加显式翻页入口和章节选中态，目录长按中键不会进入旧示例菜单。
- 固件 0.3.3-rc25：阅读设置的刷新图标按预览改为清晰的粗线回环；阅读页底部进度条与章节、页码信息铺满内容宽度。系统默认使用内建思源黑体，移除“默认无衬线”名称；阅读和系统字体选择后返回设置页仍显示中文字体名称。首页题头改为“首页”，坐标与字号对齐书架页。
- 固件 0.3.3-rc24：阅读设置底栏第四项新增刷新设置，可手动整屏全刷，或选择每阅读 5、10、15 页整屏清残影一次，默认 15 页。文件管理首页直接提供 WiFi 传书、热点传书和 USB 读卡入口；书架「导入」进入文件管理，传输结束后返回文件管理并省去额外白屏。
- 固件 0.3.3-rc23：阅读翻页和周期清残影仅更新正文与底部进度区，顶部状态栏和书名保持稳定；进度条下方的章节信息从 15 像素增至 20 像素，并独立保留右侧页码与全书进度。
- 固件 0.3.3-rc22：联网后对时直接复用已连接的 WiFi，并在写入设备时钟后核对结果；配网列表和遗忘确认页的底栏恢复切换功能。首次刷入本版时将亚克力书架设为显示样式，此后保留手动选择。移除阅读翻页动画及其设置，字体设置恢复紧凑面板，普通翻页继续使用单次灰阶刷新。阅读目录改为独立单行卡片，标题测量与绘制使用相同字体以避免重叠；EPUB 3 目录兼容更多标记格式，目录缺失的正文标题在关闭书籍时写回索引。
- 固件 0.3.3-rc21：设置页和联网状态栏改用 UX v17 预览图同源的 WiFi 灰阶像素图形，保留圆润的双段弧线与水滴形底部；状态栏的图标位置和电池比例同步对齐参考稿。
- 固件 0.3.3-rc20：优化大 EPUB 的 ZIP 目录与流式解压，单个无关大资源不再阻止整本书打开；目录缺项时按需读取章节正文标题。阅读排版支持书内外部 CSS 的有界子集，修正中文左标点折行后的对齐宽度；插图识别补充惰性加载、SVG 图片标签与单图 SVG 包装，并解码图片路径中的实体。
- 固件 0.3.3-rc19：移除微信读书入口和在线阅读模块；底栏第四项改为“设置”，点击后直接进入设置页。同时移除尚未实现的蓝牙设置与 Calibre 传输入口。首次启动新版固件时清除旧版保存的微信读书登录凭据，保留本地书籍。

## 2026-10-01 · Codex

- 固件 0.3.3-rc18：撤销整页 8 相短 DU；系统页面切换改为 30 相 8 灰阶差分 GL16，每 12 次同步做一次完整 GC16 清残影，降低残影且不做延迟二次刷新。

- 固件 0.3.3-rc17：文件首页改为真实 TF 根目录卡片列表并直接提供文件操作；系统页面切换改为 8 相差分 DU，累计 20 次同步做一次 GC16 清残影，取消每次导航的明显灰阶整页刷新。
- 固件 0.3.3-rc16：把参考稿 S03 文件操作面板、S05 传输方式选择与 S06 浏览器二维码传输页写入固件；提高阅读字体选择、统计、书签和最近阅读面板的文字、边框与选中状态对比度，并统一状态栏 WiFi 图标。
- 固件 0.3.3-rc15：连续 DU 翻页由 8 段 6 相位缩短为 6 段 5 相位，减少约四分之一动画扫描；定稿阶段对变化像素使用加强差分 GL16，压实新文字并继续擦白旧文字，改善翻页后的新旧页叠影。
- 固件 0.3.3-rc14：区分默认翻页与翻页动画。默认翻页保持单次 GL16；动画档复用示例工程连续 DU 状态机，以 8 段方向性边界同步擦除旧页、写入新页，下一页从右向左、上一页从左向右，最后只补抗锯齿与插图灰阶，避免再次整页刷新。
- 固件 0.3.3-rc13：阅读翻页改为单次 GL16 灰阶差分刷新。下一页一次提交，页眉、正文、插画和页脚在同一遍刷新中完成，不再横向分段擦入；普通缓存未命中也使用 GL16，仍每 14 页用 GC16 清理残影。
- 固件 0.3.3-rc12：降低首页、书架、文件管理和应用之间切换时的刷新存在感。普通切换保持差分 GL16；局部按钮、菜单、封面和弹窗刷新不再累计整屏清残影次数，系统页清理周期延长到 40 次整页切换，减少频繁黑白闪动。阅读页仍独立保持每 14 页清残影。
- 固件 0.3.3-rc11：参考 KOReader 软件擦入动画重做阅读翻页。下一页从右向左、上一页从左向右；中间帧使用灰阶窄条刷新，末帧以 GL16 定稿整页，清除原快速 DU 动画在文字和抗锯齿边缘留下的大量残影。
- 固件 0.3.3-rc10：阅读页底部章节与进度信息改用独立固定字号，开合阅读设置后不再随正文或系统字号变化；字号略微缩小，并显示“第几章 + 章节名”。同时加入阅读翻页动画选项、图像灰阶抖动、票根锁屏封面背景、WiFi 连接驻留、目录排版和 EPUB 插画兼容修复，并减少常规系统页面的整屏刷新。
- 固件 0.3.3-rc9：修复阅读字体滑块拖动时白块遮住滑轨；扩大滑块局部刷新范围并改用灰阶刷新。首页封面、书架分页和目录分页改为内容区局部更新，减少整屏刷新。目录页移除旧底部入口并重新排版。修复微信读书 HTTPS 登录链接被二维码校验错误拒绝。书籍名称统一取文件名，文件管理新增打开、复制、重命名和删除操作，连接 WiFi 时状态栏显示无线标识。
- 固件 0.3.3-rc8：修复 WiFi 扫描出现 `ESP_ERR_NO_MEM`。扫描前释放可重建的字形缓存，扫描结果移至外部内存并限制为界面实际可显示的 16 个网络；调低单用户传书不需要的无线缓冲预留，并增加失败阶段和剩余内存诊断。
- 固件 0.3.3-rc7：重写设备端 WiFi 扫描生命周期。扫描建立标准 STA 接口，兼容目标机的 remote/hosted WiFi 后端；一次性读取并去重结果，短暂状态错误自动重试，覆盖中国 2.4 GHz 信道 1–13。设置入口直接进入独立配网页，开放网络可直接连接，失败状态显示原因并提供重试。
- 固件 0.3.3-rc5：重构设备端 WiFi 配网页面并修复无线模块重复初始化后失效；设置入口直接进入配网，连接状态与已保存网络可正常更新。
- 首页固定使用六本书的基础网格，不再跟随个性书架；封面集中解码后一次刷新，避免每出现一本书就整屏闪烁。书架样式精简为深色书轨、亚克力挡板和半透明书袋三种。
- 系统设置新增全局对比度；字号、边距、行距支持端点逐级微调，修复最小行距反而增大的问题。
- 长按电源键显示双圆形按钮的重启／关机菜单；关机执行主机彻底断电，短按继续进入所选票根或壁纸锁屏。
- 首页恢复为六本书与继续阅读布局，书名保持单行且最多八个字，并同步展示所选书架效果；书架页取消书名，数量与页码移到底部中央。TF 目录补充文件夹、文件图形和容量信息，WiFi 传书页移除重复的 USB 入口，设置中的 WiFi 直接进入配网。应用图标加粗到适合墨水屏的清晰线宽，字体、书架与锁屏选项统一使用浅灰选中态。
- 底栏应用四宫格下移以统一视觉中心；应用页保留圆角外框，内部微信读书与设置改用和底栏一致的极简细线图标。
- 书架标题右侧新增「管理」并与「导入」并排，直接进入批量管理；应用页移除重复的图片、传输和图书管理入口，仅保留微信读书与设置。
- 系统二级页面共用的圆形返回键缩小，保留原触控热区，兼顾视觉比例和点击容错。
- 修复 TF 卡全目录浏览的分类计数越界；文件夹使用独立探测并优先排序，进入根目录后可以继续逐层浏览。
- 阅读设置补齐实际交互：字号、边距和行距可拖动并在松手后统一重排；阅读字体超过九个时可上下翻页；阅读统计的“阅读明细”和“最近30天”进入真实数据页面。
- 阅读页顶部恢复显示当前书名并移除圆形返回键及其隐藏热区；修正新版阅读工具栏仍误用旧按钮编号的问题，目录、书签、阅读统计和字体设置分别执行对应功能。
- 字体设置面板补齐可点击的字号、页边距、行距、阅读字体和晃动翻页控制，弹层使用各自命中区域，不再与底部工具栏重叠。
- 系统字体与阅读正文字体完全分离；首次进入产品界面前再次接管已挂载的 TF 系统字体，书名和系统控件不再跟随阅读字体。字体列表用浅灰底标记当前项。
- 首页与书架按内容版本、目录和 TF 挂载状态缓存扫描结果与封面，仅在导入、删除、换目录或存储状态变化后重扫。
- EPUB 插图改为读取文件签名识别 JPEG/PNG，兼容扩展名缺失或错误的图片；提高图片容量上限和浅色插画对比度。
- 阅读翻页取消延迟二次刷新；系统页面切换改用灰阶页刷，传输状态只刷新局部区域，减少整屏闪烁和等待。
- 状态栏使用圆角电池、左侧百分比、充电闪电和满电填充；应用页改为四列圆角图标，设置与各二级页统一返回、卡片间距及更清晰的系统字号。
- 文件管理显示 TF 目录并支持自选书籍/字体目录；USB 读卡直接进入挂载流程。票根与自选图片归入锁屏样式。
- 修复继续阅读书名尾部 `=This`、长书名截断、书架透明挡板辨识度及旧示例页面残留入口。

## 2026-09-30 · Codex

- 书架新增第四种半透明书袋样式，样式选择页使用四种实际书架首行的缩略图；票根锁屏在阅读中保留当前正文为背景，其他页面使用灰阶斜格纹。状态栏在充电时显示闪电标识。
- 应用页增加微信读书入口，目前支持已保存 WiFi 下的扫码登录、云端书架同步和退出登录；书籍缓存与本机阅读仍在开发中，尚未作为完整功能交付。
- 系统页面三颗触控键改为返回／首页／返回；阅读正文继续使用翻页与中键设置。系统小字约放大 1.2 倍，阅读正文大小不变。文字边缘、图标与分割线加深；书籍封面、插图、图片和票根壁纸提高灰阶对比度。
- 首页改为紧凑的继续阅读卡、近七天实测阅读时长与柱状图、书架摘要和四本最近阅读；书架提供默认网格、深色书轨、亚克力挡板三种样式，选项保存在设备上。
- 应用页改为浅色九宫格，框线加深并换用 Pico 灰阶图标；设置页改为卡片分组和较醒目的灰色图标，字号与间距集中在一个设置页。
- 亚克力书架挡板增加灰阶模糊和更明显的半透明覆盖，样式选择页共用同一绘制效果。状态栏使用圆角电池并在左侧显示百分比，系统界面字号略增。
- 书架切页后复用已扫描书目和封面，在传书、USB 退出或存储状态变化后重扫。重新生成内建字体子集，补齐当前界面文字；移除打开书籍时底部临时加载提示。
- 产品界面统一使用 Pico 名称。书架仅显示导入书籍，每页九本。底栏第四项改为应用，集中放置图片、传输、图书管理和设置。
- 本机书名编辑加入可点击定位、左右移动的插入光标，可在书名中间插入或删除文字。设置改为分组列表，加入 WiFi、蓝牙和日期时间入口。
- 手动设时与 WiFi 对时写入 PMU；开机读取 PMU 时钟恢复系统时间。WiFi 对时使用已保存网络，完成后关闭无线连接。

## 2026-09-29 · Codex

- 系统界面改用独立内建的 Noto Sans SC Regular 子集（OFL-1.1）；阅读正文仍可选择其他字体。修复书架设置入口的残留重绘与旧设置页代码。
- 设置页增加本机日期时间编辑（北京时间 UTC+8），保存后同步系统时间和 PMU；EPUB 章节题头使用目录标题，不再把正文前置引言当作章节名。
- 首页封面缓存在 PSRAM，返回首页时按路径、文件大小与修改时间复用，书籍顺序变化也可复用。
- 底部四栏图标改为细线风格：首页房屋、书架斜立书脊、文件夹与设置宫格。
- 首页阅读热力图改为最近 30 个自然日，使用实际阅读时长；设备时钟未同步时不显示虚构的日期数据。
- 加入首页、书架、文件管理、设置四栏导航；文件页集中书籍、图片、字体和 USB/Wi-Fi 传输入口。跨页面与进入阅读时执行整屏刷新。
- EPUB 章节第一页单独显示章节题头；书名改为本机拼音候选输入与保存，无需打开网页。阅读字体入口直接进入 TF 卡字体目录。
- 预览图使用苹方；固件保留可分发的无衬线后备字体，支持从 TF 卡加载兼容 TTF/OTF 文件，未随固件分发苹果字体。
- 固件 0.3.2：书架封面下只显示书名。首次打开书架或滑到未缓存的页面时先显示书架，再逐本补齐封面；生成的缩略图保存在 TF 卡隐藏目录，后续开机和翻页直接读取，换书后自动更新。
- 修正开机时误导性的「初始化内置存储」提示；既有内置分区仍按正常流程挂载，不会在每次开机格式化。字号和间距重排只测量字宽，减少额外的进度保存和字号二次刷屏；间距变化先快速刷新，闲置后整理灰阶。

## 2026-09-28 · Codex

- 固件 0.3.1：阅读页进度条改为圆角并移除外伸章节刻度；书架继续阅读区增加最近一本的封面、章节与页码信息。
- 票根锁屏上下使用裁纸缺口，背景由最近阅读的 EPUB 封面生成灰阶壁纸；无内嵌封面的书使用书名占位背景。

- 固件 0.3.0：书架移除底部翻页与菜单，改用左右滑动；顶部「设置」整合图书管理、图片和传输。
- 图书详情加入手动编辑书名入口；WiFi 网页可编辑 `book`、`books` 和内置图书的显示书名，独立保存而不改 EPUB 或进度。
- 票根锁屏显示最近阅读书籍、封面、进度和实测阅读统计；WiFi 网页同步设备时间以启用今日统计，时钟无效时明确提示。

- 主菜单收敛为书架、图片显示、传输；书架优先读取 EPUB 的书名、作者，显示封面和继续阅读入口，进入书架不再自动跳到正文。
- EPUB 正文支持 JPEG/PNG 图片独立成页；阅读设置增加字号、行距、段距和扩展字体。普通翻页改为快速差分，闲置后灰阶整理，周期整屏清残影。
- 传输页增加 USB 读卡模式，安全弹出后恢复 TF 卡本机挂载；USB 活跃时暂停本机卡访问和锁屏。
- 固件版本 0.2.0 已使用 DIO/80 MHz 写入 ESP32-S3，哈希验证通过，启动日志确认 SD 卡挂载与书架扫描。
- 增加图片对比页：内置灰阶测试图，读取 TF 卡 `pictures`/`images` 下的 JPG/PNG，并通过传书网页上传图片；图片切换时整屏灰阶刷新。
- 阅读中键单击进入页面设置，长按回书架；阅读页移除右下角返回图标；书架与字体列表忽略 macOS 的隐藏辅助文件。
- 书架同时扫描 TF 卡根目录的 `book` 与 `books` 文件夹，兼容已放入单数目录的书籍，传书仍保存到 `books`。
- 图书书架改为每页六本的封面网格；自动从 EPUB 2/3 的 OPF 声明提取 JPEG/PNG 封面，生成电子墨水屏灰阶缩略图，缺失时显示书名占位封面。
- 阅读工具栏加入宋体、黑体、楷体快捷切换，字体从 TF 卡 `fonts` 目录读取；附带三种开源中文字体。
- 提供合并固件镜像与基于 ESP Web Tools 的本地 Chrome 刷机页面。

## 2026-09-27 · UNSaWEN

- 图书阅读：支持 TF 卡或内置存储的 UTF-8/GBK TXT 与纯文本 EPUB，目录、逐书续读、字号、手势翻页及默认关闭的实验晃动翻页。
  Read UTF-8/GBK TXT and text-only EPUB from TF or internal storage, with a TOC, per-book resume, font sizes, gestures and optional experimental shake-to-turn.
- 图书管理：增加拼音/首字母/英文搜索、来源筛选、名称/最近阅读排序、详情，以及分别确认的单本/批量删除和清进度。
  Add pinyin/initials/English search, source filters, name/recent sorting, details and separately confirmed single/batch deletion or progress reset.
- WiFi 传书：支持设备热点和已有网络、触屏/网页配网、连接与网址二维码；网页管理当前存储，支持确认替换、取消和重试；停止后返回进入前的位置。
  Transfer over the device hotspot or an existing network, with touchscreen/web provisioning and connection/URL QR codes. Manage current storage in the browser, confirm replacements, cancel or retry uploads, and return to the entry location on stop.
- 可靠性：处理保存失败与部分成功重试、中文文件名和覆盖中断恢复；修复显示欠载后相位队列残留导致的死锁；检测 TF 失效后停止相关消费者并回退字体，插回后需显式重挂。
  Handle save failures, partial-operation retries, Chinese filenames and interrupted replacements. Fix a display queue deadlock after underrun; stop affected consumers and fall back fonts on TF loss, requiring explicit remount after reinsertion.
