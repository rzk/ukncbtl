/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "SdlMediaDialog.h"
#include <gtest/gtest.h>
#include <memory>
#include <thread>

TEST(SdlMediaDialog, HandsWorkerThreadResultToMainThread)
{
    auto state = std::make_shared<sdl_media_dialog::State>();
    ASSERT_TRUE(state->Begin());
    EXPECT_TRUE(state->Pending());
    EXPECT_FALSE(state->Begin());
    std::thread worker([state] {
        state->Complete({media_slots::Kind::hardDisk, 1, sdl_media_dialog::Status::selected, "disk.img"});
    });
    worker.join();
    EXPECT_FALSE(state->Pending());
    EXPECT_FALSE(state->Begin()); // An unread result cannot be overwritten.
    const auto result = state->Take();
    ASSERT_TRUE(result);
    EXPECT_EQ(result->kind, media_slots::Kind::hardDisk);
    EXPECT_EQ(result->slot, 1);
    EXPECT_EQ(result->status, sdl_media_dialog::Status::selected);
    EXPECT_EQ(result->path, "disk.img");
    EXPECT_FALSE(state->Take());
    EXPECT_TRUE(state->Begin());
    state->Complete({media_slots::Kind::floppy, 0, sdl_media_dialog::Status::canceled, {}});
    ASSERT_TRUE(state->Take());
}

TEST(SdlMediaDialog, PendingCallbackCanOutliveOwner)
{
    auto owner = std::make_shared<sdl_media_dialog::State>();
    std::weak_ptr<sdl_media_dialog::State> weak = owner;
    ASSERT_TRUE(owner->Begin());
    auto callbackState = owner;
    owner.reset();
    ASSERT_FALSE(weak.expired());
    callbackState->Complete({media_slots::Kind::floppy, 3, sdl_media_dialog::Status::failed, {}});
    callbackState.reset();
    EXPECT_TRUE(weak.expired());
}
