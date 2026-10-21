/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：push_sink 的宿主测试替身入口，与同目录其它组件替身同一形制：只转发到
 * transfer_ui_test_env.h，真正的类型与函数替身都在那里定义。真实组件的
 * push_sink.h 不带函数体的原型声明，若被包含进来会和 env 里的 static inline
 * 定义冲突，故这里整头遮蔽。
 * English: host shim entry for push_sink, same shape as the sibling component
 * shims here: it forwards to transfer_ui_test_env.h, where the types and
 * function doubles live. The real push_sink.h carries prototype declarations
 * that would clash with the static inline doubles, so it is shadowed whole.
 * 冻结：仅供测试。/ Frozen: tests only.
 */
#include "transfer_ui_test_env.h"
