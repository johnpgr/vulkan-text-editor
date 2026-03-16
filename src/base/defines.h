#pragma once

#define internal static

#define BIT(x) (1ULL << (x))
#define KB 1024ULL
#define MB (KB * KB)
#define GB (MB * KB)
#define TB (GB * KB)

#include <utility>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vulkan/vulkan.h>

#include "base/typedef.h"
#include "base/log.h"
#include "base/core.h"

#include "base/memory.h"
#include "base/arena.h"
#include "base/string.h"
#include "base/array.h"
#include "base/math.h"
#include "platform/platform.h"
