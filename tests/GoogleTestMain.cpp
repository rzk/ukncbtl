/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "emubase/Processor.h"
#include <gtest/gtest.h>

namespace
{
class ProcessorEnvironment : public testing::Environment
{
    void SetUp() override { CProcessor::Init(); }
    void TearDown() override { CProcessor::Done(); }
};
}

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new ProcessorEnvironment);
    return RUN_ALL_TESTS();
}
