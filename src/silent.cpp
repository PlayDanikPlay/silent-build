#include "silent.h"

#include <dobby.h>
#include <link.h>
#include <pthread.h>
#include <sys/uio.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstring>

namespace silent {
namespace {

constexpr uintptr_t kCommandDispatchRva = 0x07A7237C;
constexpr uintptr_t kGetFromPoolRva = 0x07A8417C;
constexpr uintptr_t kGetMainCameraRva = 0x04A5E1FC;
constexpr uintptr_t kTransformGetPositionRva = 0x04A9973C;
constexpr uintptr_t kColliderBoundsRva = 0x067C5D50;
constexpr uintptr_t kRaycastTestRva = 0x067BD9D4;

constexpr uintptr_t kPlayerTypeInfoRva = 0x0BC27FB4;
constexpr uintptr_t kStatsTypeInfoRva = 0x0BC22B54;
constexpr uintptr_t kFeatureTypeInfoRva = 0x0BC21BC4;

constexpr uintptr_t kPlayerRegistryOffset = 0x18;
constexpr uintptr_t kPlayerStatsOffset = 0x40;
constexpr uintptr_t kPlayerViewOffset = 0x48;
constexpr uintptr_t kPlayerTeamOffset = 0x49;
constexpr uintptr_t kStatsHealthOffset = 0x08;

constexpr uintptr_t kRegistryTableOffset = 0x10;
constexpr uintptr_t kTableCountOffset = 0x0C;
constexpr uintptr_t kTableEntriesOffset = 0x10;
constexpr uintptr_t kTableEntryStride = 0x10;

constexpr uintptr_t kFeatureCollidersOffset = 0x0C;
constexpr uintptr_t kHitBoxBoneOffset = 0x10;
constexpr uintptr_t kHitBoxColliderOffset = 0x18;
constexpr uintptr_t kHitBoxOwnerOffset = 0x1C;
constexpr uintptr_t kCachedPointerOffset = 0x08;
constexpr uintptr_t kArrayLengthOffset = 0x0C;
constexpr uintptr_t kArrayFirstOffset = 0x10;

constexpr uintptr_t kCommandIdOffset = 0x08;
constexpr uintptr_t kCommandAimFlagOffset = 0x14;
constexpr uintptr_t kCommandPitchOffset = 0x18;
constexpr uintptr_t kCommandYawOffset = 0x1C;
constexpr uintptr_t kCommandMoveOffset = 0x28;

constexpr uint8_t kPlayerPoolId = 1;
constexpr int32_t kBoneHead = 9;
constexpr int32_t kBoneNeck = 10;
constexpr int32_t kMaximumHitBoxes = 32;
constexpr int32_t kMaximumEntries = 64;
constexpr int32_t kMaximumPlayers = 64;

constexpr int kWorldMask = (1 << 14) | (1 << 4);
constexpr int kIgnoreTriggers = 1;
constexpr float kTargetMargin = 0.06F;
constexpr float kMinimumDistance = 0.15F;
constexpr float kRadiansToDegrees = 57.2957795F;
constexpr float kDegreesToRadians = 0.01745329252F;

struct Vector3 {
    float x;
    float y;
    float z;
};

struct Bounds {
    float center[3];
    float extents[3];
};

struct ReadOnlySpan {
    uintptr_t data;
    int32_t length;
};

struct PhysicsScene {
    int32_t index;
    int32_t version;
};

struct Ray {
    float origin[3];
    float direction[3];
};

using CommandDispatch = void (*)(void*, void*, void*, int32_t, void*);
using GetFromPool = void (*)(ReadOnlySpan*, void*, uint8_t, void*);
using GetMainCamera = void* (*)(void*);
using TransformGetPosition = void (*)(void*, Vector3*);
using ColliderGetBounds = void (*)(void*, Bounds*);
using RaycastTest = uint8_t (*)(const PhysicsScene*, const Ray*, float, int, int);

uintptr_t g_unity_base = 0;
CommandDispatch g_original_dispatch = nullptr;
GetFromPool g_original_get_from_pool = nullptr;
GetMainCamera g_get_main_camera = nullptr;
TransformGetPosition g_transform_position = nullptr;
ColliderGetBounds g_collider_bounds = nullptr;
RaycastTest g_raycast = nullptr;
std::atomic<uintptr_t> g_manager{0};

bool Read(uintptr_t address, void* output, size_t size) {
    if (address < 0x10000U) {
        return false;
    }
    iovec local{output, size};
    iovec remote{reinterpret_cast<void*>(address), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
}

template <typename T>
bool ReadValue(uintptr_t address, T* output) {
    return Read(address, output, sizeof(T));
}

uintptr_t ClassAt(uintptr_t rva) {
    uintptr_t value = 0;
    uintptr_t image = 0;
    if (ReadValue(g_unity_base + rva, &value) && value != 0 && ReadValue(value, &image) &&
        image != 0) {
        return value;
    }
    return 0;
}

bool HasClass(uintptr_t object, uintptr_t expected) {
    uintptr_t klass = 0;
    return object > 0x10000U && (object & 3U) == 0U && expected != 0 &&
           ReadValue(object, &klass) && klass == expected;
}

float Normalise(float degrees) {
    degrees = std::fmod(degrees, 360.0F);
    if (degrees < 0.0F) {
        degrees += 360.0F;
    }
    return degrees;
}

bool EyePosition(Vector3* output) {
    if (g_get_main_camera == nullptr || g_transform_position == nullptr) {
        return false;
    }
    void* camera = g_get_main_camera(nullptr);
    if (camera == nullptr) {
        return false;
    }
    uintptr_t transform = 0;
    if (!ReadValue(reinterpret_cast<uintptr_t>(camera) + kCachedPointerOffset, &transform) ||
        transform == 0) {
        return false;
    }
    g_transform_position(camera, output);
    return std::isfinite(output->x) && std::isfinite(output->y) && std::isfinite(output->z);
}

bool Visible(const Vector3& eye, const Vector3& point) {
    if (g_raycast == nullptr) {
        return true;
    }
    const float dx = point.x - eye.x;
    const float dy = point.y - eye.y;
    const float dz = point.z - eye.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distance) || distance < kMinimumDistance) {
        return true;
    }
    const Ray ray{{eye.x, eye.y, eye.z},
                  {dx / distance, dy / distance, dz / distance}};
    const PhysicsScene scene{0, 0};
    return g_raycast(&scene, &ray, distance - kTargetMargin, kWorldMask, kIgnoreTriggers) == 0U;
}

uintptr_t FindFeature(uintptr_t player, uintptr_t feature_class) {
    uintptr_t registry = 0;
    uintptr_t table = 0;
    int32_t count = 0;
    if (!ReadValue(player + kPlayerRegistryOffset, &registry) || registry == 0 ||
        !ReadValue(registry + kRegistryTableOffset, &table) || table == 0 ||
        !ReadValue(table + kTableCountOffset, &count) || count <= 0) {
        return 0;
    }
    if (count > kMaximumEntries) {
        count = kMaximumEntries;
    }
    static const uintptr_t kValueOffsets[] = {0x08, 0x0C};
    for (int32_t i = 0; i < count; ++i) {
        const uintptr_t entry =
            table + kTableEntriesOffset + static_cast<uintptr_t>(i) * kTableEntryStride;
        for (uintptr_t offset : kValueOffsets) {
            uintptr_t value = 0;
            if (ReadValue(entry + offset, &value) && HasClass(value, feature_class)) {
                return value;
            }
        }
    }
    return 0;
}

bool HeadPoint(uintptr_t player, uintptr_t feature_class, Vector3* output) {
    const uintptr_t feature = FindFeature(player, feature_class);
    uintptr_t colliders = 0;
    int32_t length = 0;
    if (feature == 0 || !ReadValue(feature + kFeatureCollidersOffset, &colliders) ||
        colliders == 0 || !ReadValue(colliders + kArrayLengthOffset, &length) || length <= 0 ||
        length > kMaximumHitBoxes) {
        return false;
    }

    for (int32_t i = 0; i < length; ++i) {
        uintptr_t hit_box = 0;
        uintptr_t owner = 0;
        int32_t bone = 0;
        if (!ReadValue(colliders + kArrayFirstOffset + static_cast<uintptr_t>(i) * 4U,
                       &hit_box) ||
            hit_box == 0 || !ReadValue(hit_box + kHitBoxOwnerOffset, &owner) ||
            owner != player || !ReadValue(hit_box + kHitBoxBoneOffset, &bone) ||
            (bone != kBoneHead && bone != kBoneNeck)) {
            continue;
        }
        uintptr_t collider = 0;
        uintptr_t native = 0;
        if (!ReadValue(hit_box + kHitBoxColliderOffset, &collider) || collider == 0 ||
            !ReadValue(collider + kCachedPointerOffset, &native) || native == 0) {
            continue;
        }
        Bounds bounds{};
        g_collider_bounds(reinterpret_cast<void*>(native), &bounds);
        const float span = bounds.extents[0] + bounds.extents[1] + bounds.extents[2];
        if (!std::isfinite(bounds.center[0]) || !std::isfinite(bounds.center[1]) ||
            !std::isfinite(bounds.center[2]) || !std::isfinite(span) || span <= 0.0F ||
            span > 6.0F) {
            continue;
        }
        output->x = bounds.center[0];
        output->y = bounds.center[1];
        output->z = bounds.center[2];
        return true;
    }
    return false;
}

bool LocalTeam(const ReadOnlySpan& span, uintptr_t player_class, uint8_t* team) {
    for (int32_t i = 0; i < span.length; ++i) {
        uintptr_t player = 0;
        uint8_t view = 0;
        if (!ReadValue(span.data + static_cast<uintptr_t>(i) * sizeof(uintptr_t), &player) ||
            !HasClass(player, player_class) ||
            !ReadValue(player + kPlayerViewOffset, &view) || view == 0U) {
            continue;
        }
        return ReadValue(player + kPlayerTeamOffset, team);
    }
    return false;
}

bool ChooseHead(const Vector3& eye, float pitch, float yaw, Vector3* output) {
    const uintptr_t manager = g_manager.load(std::memory_order_acquire);
    if (manager == 0 || g_original_get_from_pool == nullptr || g_collider_bounds == nullptr) {
        return false;
    }

    ReadOnlySpan span{};
    g_original_get_from_pool(&span, reinterpret_cast<void*>(manager), kPlayerPoolId, nullptr);
    if (span.data == 0 || span.length <= 0 || span.length > kMaximumPlayers) {
        return false;
    }

    const uintptr_t player_class = ClassAt(kPlayerTypeInfoRva);
    const uintptr_t stats_class = ClassAt(kStatsTypeInfoRva);
    const uintptr_t feature_class = ClassAt(kFeatureTypeInfoRva);
    if (player_class == 0 || feature_class == 0) {
        return false;
    }

    uint8_t local_team = 0xFFU;
    LocalTeam(span, player_class, &local_team);

    const float pitch_radians = pitch * kDegreesToRadians;
    const float yaw_radians = yaw * kDegreesToRadians;
    const float cosine_pitch = std::cos(pitch_radians);
    const Vector3 forward{cosine_pitch * std::sin(yaw_radians), -std::sin(pitch_radians),
                          cosine_pitch * std::cos(yaw_radians)};

    float best_dot = -2.0F;
    bool found = false;
    for (int32_t i = 0; i < span.length; ++i) {
        uintptr_t player = 0;
        uintptr_t stats = 0;
        uint16_t health = 0;
        uint8_t view = 0;
        uint8_t team = 0;
        if (!ReadValue(span.data + static_cast<uintptr_t>(i) * sizeof(uintptr_t), &player) ||
            !HasClass(player, player_class) ||
            !ReadValue(player + kPlayerViewOffset, &view) || view != 0U ||
            !ReadValue(player + kPlayerTeamOffset, &team) || team == local_team ||
            !ReadValue(player + kPlayerStatsOffset, &stats) ||
            !HasClass(stats, stats_class) ||
            !ReadValue(stats + kStatsHealthOffset, &health) || health == 0U) {
            continue;
        }

        Vector3 head{};
        if (!HeadPoint(player, feature_class, &head)) {
            continue;
        }

        const float dx = head.x - eye.x;
        const float dy = head.y - eye.y;
        const float dz = head.z - eye.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(distance) || distance < kMinimumDistance) {
            continue;
        }
        const float dot =
            (forward.x * dx + forward.y * dy + forward.z * dz) / distance;
        if (dot <= best_dot || !Visible(eye, head)) {
            continue;
        }
        best_dot = dot;
        *output = head;
        found = true;
    }
    return found;
}

void ApplyToCommand(void* command_object) {
    if (command_object == nullptr) {
        return;
    }
    auto* fields = static_cast<uint8_t*>(command_object);
    uint32_t command_id = 0;
    std::memcpy(&command_id, fields + kCommandIdOffset, sizeof(command_id));
    if (command_id == 0) {
        return;
    }

    float game_pitch = 0.0F;
    float game_yaw = 0.0F;
    std::memcpy(&game_pitch, fields + kCommandPitchOffset, sizeof(game_pitch));
    std::memcpy(&game_yaw, fields + kCommandYawOffset, sizeof(game_yaw));
    if (!std::isfinite(game_pitch) || !std::isfinite(game_yaw)) {
        return;
    }

    Vector3 eye{};
    if (!EyePosition(&eye)) {
        return;
    }

    Vector3 head{};
    if (!ChooseHead(eye, game_pitch, game_yaw, &head)) {
        return;
    }

    const float dx = head.x - eye.x;
    const float dy = head.y - eye.y;
    const float dz = head.z - eye.z;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    if (horizontal < 0.01F) {
        return;
    }

    const float pitch = Normalise(-std::atan2(dy, horizontal) * kRadiansToDegrees);
    const float yaw = Normalise(std::atan2(dx, dz) * kRadiansToDegrees);
    if (!std::isfinite(pitch) || !std::isfinite(yaw)) {
        return;
    }

    auto* move = reinterpret_cast<float*>(fields + kCommandMoveOffset);
    if (std::isfinite(move[0]) && std::isfinite(move[1])) {
        const float delta = (yaw - game_yaw) * kDegreesToRadians;
        const float cosine = std::cos(delta);
        const float sine = std::sin(delta);
        const float forward = move[0];
        const float strafe = move[1];
        move[0] = forward * cosine - strafe * sine;
        move[1] = forward * sine + strafe * cosine;
    }

    std::memcpy(fields + kCommandPitchOffset, &pitch, sizeof(pitch));
    std::memcpy(fields + kCommandYawOffset, &yaw, sizeof(yaw));
    fields[kCommandAimFlagOffset] = 1U;
}

void HookedGetFromPool(ReadOnlySpan* output, void* self, uint8_t pool, void* method) {
    g_original_get_from_pool(output, self, pool, method);
    if (pool == kPlayerPoolId && self != nullptr) {
        g_manager.store(reinterpret_cast<uintptr_t>(self), std::memory_order_release);
    }
}

void HookedCommandDispatch(void* self,
                           void* timing_reference,
                           void* prediction_items,
                           int32_t item_count,
                           void* command_object) {
    g_original_dispatch(self, timing_reference, prediction_items, item_count, command_object);
    ApplyToCommand(command_object);
}

int FindUnityCallback(dl_phdr_info* info, size_t, void* opaque) {
    if (info == nullptr || info->dlpi_name == nullptr ||
        std::strstr(info->dlpi_name, "libunity.so") == nullptr) {
        return 0;
    }
    *static_cast<uintptr_t*>(opaque) = static_cast<uintptr_t>(info->dlpi_addr);
    return 1;
}

void* Worker(void*) {
    uintptr_t base = 0;
    for (int attempt = 0; attempt < 600 && base == 0; ++attempt) {
        dl_iterate_phdr(FindUnityCallback, &base);
        if (base == 0) {
            usleep(500000);
        }
    }
    if (base == 0) {
        return nullptr;
    }

    g_unity_base = base;
    g_get_main_camera = reinterpret_cast<GetMainCamera>(base + kGetMainCameraRva);
    g_transform_position =
        reinterpret_cast<TransformGetPosition>(base + kTransformGetPositionRva);
    g_collider_bounds = reinterpret_cast<ColliderGetBounds>(base + kColliderBoundsRva);
    g_raycast = reinterpret_cast<RaycastTest>(base + kRaycastTestRva);

    DobbyHook(reinterpret_cast<void*>(base + kGetFromPoolRva),
              reinterpret_cast<void*>(HookedGetFromPool),
              reinterpret_cast<void**>(&g_original_get_from_pool));
    DobbyHook(reinterpret_cast<void*>(base + kCommandDispatchRva),
              reinterpret_cast<void*>(HookedCommandDispatch),
              reinterpret_cast<void**>(&g_original_dispatch));
    return nullptr;
}

}

bool Install() {
    pthread_t thread{};
    if (pthread_create(&thread, nullptr, Worker, nullptr) != 0) {
        return false;
    }
    pthread_detach(thread);
    return true;
}

}

__attribute__((constructor)) static void Entry() { silent::Install(); }
