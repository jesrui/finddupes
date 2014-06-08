#!/bin/bash

FD=./finddupes
#FD="valgrind --leak-check=full ./finddupes"
D=testdir

test_symlink_file()
{
#    set -x
    res=$($FD $D/two $D/symlink_two)
#    set +x
    assertEquals $? 0
    exp=
    assertEquals "$exp" "$res"

    res=$($FD --symlinks $D/two $D/symlink_two)
    assertEquals $? 0
    exp=$(cat<<'END'
testdir/two
testdir/symlink_two
END
)
    assertEquals "$exp" "$res"
}

test_big_file()
{
    res=$($FD $D/big)
    assertEquals $? 0
    exp=$(cat<<'END'
testdir/big/big2_copy
testdir/big/big2
END
)
    assertEquals "$exp" "$res"
}

test_unique()
{
    res=$($FD --unique $D/big)
    assertEquals $? 0
    exp=$(cat<<'END'
testdir/big/big1
END
)
    assertEquals "$exp" "$res"
}

test_recursive()
{
    res=$($FD --recursive $D/)
    assertEquals $? 0
    exp=$(cat<<'END'
testdir/two
testdir/twice_one
testdir/recursed_a/two

testdir/seven
testdir/recursed_b/seven

testdir/zero_a
testdir/zero_b

testdir/recursed_b/one
testdir/recursed_a/one

testdir/with spaces a
testdir/with spaces b

testdir/big/big2_copy
testdir/big/big2

testdir/recursed_b/three
testdir/recursed_b/two_plus_one

END
)
    assertEquals "$exp" "$res"
}

. shunit2
