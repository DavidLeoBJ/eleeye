# 引擎稳定版分支（feature/search-score）
## 定位
本分支基于 xqbase/eleeye（象眼）上游版本，核心目标是将其移植到安卓平台。
所有源码修改均围绕**适配NDK编译工具链、生成安卓可用的共享库**展开，未改动核心博弈搜索算法（着法生成、局面评估、Alpha-Beta搜索等仍完全沿用上游Eleeye实现），剥离了开局库管理及残局探索相关的非核心代码，无UCCI/管道等桌面端冗余逻辑。
**本分支新增了开局ponder输出能力，为安卓App端特有的交互优化。**

## 适配NDK的必要修改（不改则无法通过NDK编译，无法生成安卓so）
因NDK编译环境与桌面端存在差异，为适配安卓运行要求，对上游源码做了以下必要调整：
1. **编码适配**：NDK编译器对源码编码敏感，上游部分文件为GBK编码，需统一调整为utf-8以避免编译乱码报错
2. **静态链接配置**：NDK环境下采用静态链接libc++，避免so依赖`libc++_shared.so`，减小分发成本并提升兼容性
3. **冗余逻辑裁剪**：移除UCCI协议、管道通信等桌面端专属逻辑，仅保留JNI调用路径，缩小so体积并避免安卓端兼容问题
4. **JNI胶水层新增**：新增6个JNI接口供安卓端调用（nativeVersion / nativeSearch / nativeEvaluate / nativeSearchAndEvaluate / nativeSetBookPath / nativeSetBookEnabled），是跨平台调用的必要适配层
5. **评分/mateStr透传**：在JNI层将上游计算的评分、实锤绝杀步数（`#X`格式）正确透传给Java层，开局库命中时评分为0

## 使用说明
- 编译安卓so：执行`build_android_all.sh`，一键生成`jniLibs/arm64-v8a/`和`jniLibs/x86_64/`双架构so
- 适配原则：核心博弈算法若有优化需求，建议同步上游Eleeye项目；本分支仅维护安卓端NDK适配逻辑

> 上游引擎：Eleeye（象眼），xqbase/eleeye