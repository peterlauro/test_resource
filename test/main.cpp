#include <gtest/gtest.h>
#include <exception>

int main(int argc, char* argv[])
{
    try
    {
        testing::InitGoogleTest(&argc, argv);

        //  check
        return RUN_ALL_TESTS();
    }
    catch (std::exception const& x)
    {
        [[maybe_unused]] auto const* why = x.what();
    }
    catch (...)
    {
    }

    return -1;
}
