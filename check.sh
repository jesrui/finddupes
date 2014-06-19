#!/bin/bash

FD=./finddupes
FD="valgrind --quiet --leak-check=full --error-exitcode=1 ./finddupes"
D=testdir

# sort output from finddupes
#
# The entries of each set are sorted first, then the sets.
#
# This is required in order to compare expected results since the order in
# which dupes are listed depends on the order of directory listings, and these
# vary from one target system to another. Listing order depends on the string
# hashing too, but I believe hashing is the same for all target systems.
#
# Note that a test must call sortdupes only if it passes some directory to
# finddupes. File arguments are fine.
#

sortdupes()
{
    sep="${1-\n}"
    setsep="${2-\n\n}"

    gawk -v RS=$setsep -v FS=$sep '
        function join(array, start, end, sep,    result, i)
        {
            if (sep == "")
                sep = " "
            else if (sep == SUBSEP) # magic value
                sep = ""
            result = array[start]
            for (i = start + 1; i <= end; i++)
                result = result sep array[i]
            return result
        }

        {
            split($0, a, FS)
            n = asort(a)
            b = join(a, 1, n, FS)
            s[j++] = b
        }

        END {
            n = asort(s)
            b = join(s, 1, n, RS);
            print b
        }'
}

tearDown()
{
    test -a $D/hardlink_two && rm $D/hardlink_two
}

test_symlink_file()
{
#    set -x
    res=$($FD $D/two $D/symlink_two)
#    set +x
    assertEquals 0 $?
    exp=
    assertEquals "$exp" "$res"

    res=$($FD --symlinks $D/two $D/symlink_two)
    assertEquals 0 $?
    exp=$(cat<<'END'
testdir/two
testdir/symlink_two

END
)
    assertEquals "$exp" "$res"
}

test_symlink_dir()
{
    res=$($FD --symlinks $D/recursed_a $D/symlink_dir | sortdupes)
    assertEquals 0 $?
    exp=$(sortdupes<<'END'
testdir/recursed_a/symlink_seven
testdir/symlink_dir/symlink_seven

END
)
    assertEquals "$exp" "$res"

    res=$($FD --symlinks --hardlinks $D/recursed_a $D/symlink_dir | sortdupes)
    assertEquals 0 $?
    exp=$(sortdupes<<'END'
testdir/recursed_a/symlink_seven
testdir/symlink_dir/symlink_seven

testdir/recursed_a/two_plus_one
testdir/symlink_dir/two_plus_one

testdir/recursed_a/one
testdir/symlink_dir/one

testdir/recursed_a/two
testdir/symlink_dir/two

testdir/recursed_a/five
testdir/symlink_dir/five

END
)
    assertEquals "$exp" "$res"
}

test_symlink_broken()
{
    res=$($FD --quiet --symlinks $D/symlink_broken 2>&1 >/dev/null)
    assertEquals 0 $?
    exp=$(cat<<'END'
stat failed: testdir/symlink_broken: No such file or directory

END
)
    assertEquals "$exp" "$res"
}

test_hardlink_file()
{
    ln $D/two $D/hardlink_two

    res=$($FD $D/two $D/hardlink_two)
    assertEquals 0 $?
    exp=
    assertEquals "$exp" "$res"

    res=$($FD --hardlinks $D/two $D/hardlink_two)
    assertEquals 0 $?
    exp=$(cat<<'END'
testdir/two
testdir/hardlink_two
END
)
    assertEquals "$exp" "$res"
}

test_empty()
{
    res=$($FD $D/zero_a $D/zero_b)
    assertEquals 0 $?
    exp=$(cat<<'END'
testdir/zero_a
testdir/zero_b

END
)
    assertEquals "$exp" "$res"

    res=$($FD --noempty $D/zero_a $D/zero_b)
    assertEquals 0 $?
    exp=
    assertEquals "$exp" "$res"
}

test_empty_symplink()
{
    res=$($FD $D/symlink_zero $D/zero_a $D/zero_b)
    assertEquals 0 $?
    exp=$(cat<<'END'
testdir/zero_a
testdir/zero_b

END
)
    assertEquals "$exp" "$res"

    res=$($FD --noempty $D/symlink_zero $D/zero_a $D/zero_b)
    assertEquals 0 $?
    exp=
    assertEquals "$exp" "$res"

    res=$($FD --noempty --symlink $D/symlink_zero $D/zero_a $D/zero_b)
    assertEquals 0 $?
    exp=
    assertEquals "$exp" "$res"
}

test_big_file()
{
    res=$($FD $D/big | sortdupes)
    assertEquals 0 $?
    exp=$(sortdupes<<'END'
testdir/big/big2_copy
testdir/big/big2

END
)
    assertEquals "$exp" "$res"
}

test_unique()
{
    res=$($FD --unique $D/big)
    assertEquals 0 $?
    exp=$(cat<<'END'
testdir/big/big1

END
)
    assertEquals "$exp" "$res"
}

test_recursive()
{
    res=$($FD --recursive $D/ | sortdupes)
    assertEquals 0 $?
    exp=$(sortdupes<<'END'
testdir/recursed_b/three
testdir/recursed_a/two_plus_one

testdir/zero_a
testdir/zero_b

testdir/two
testdir/twice_one
testdir/recursed_a/two

testdir/seven
testdir/recursed_b/seven

testdir/recursed_b/one
testdir/recursed_a/one

testdir/with spaces a
testdir/with spaces b

testdir/big/big2_copy
testdir/big/big2

END
)
    assertEquals "$exp" "$res"
}

test_separator()
{
    res=$($FD --separator='\t' --setseparator='\n' -r $D/ | sortdupes '\t' '\n')
    assertEquals 0 $?

    exp=$(sortdupes '\t' '\n' <<'END'
testdir/recursed_b/three	testdir/recursed_a/two_plus_one
testdir/zero_a	testdir/zero_b
testdir/two	testdir/twice_one	testdir/recursed_a/two
testdir/seven	testdir/recursed_b/seven
testdir/recursed_b/one	testdir/recursed_a/one
testdir/with spaces a	testdir/with spaces b
testdir/big/big2_copy	testdir/big/big2
END
)
    assertEquals "$exp" "$res"
}

test_separator_null()
{
    res=$($FD --separator '\001' --setseparator '\x00' $D/two $D/twice_one | xxd)
    assertEquals 0 $?
    exp=$(cat<<'END'
0000000: 7465 7374 6469 722f 7477 6f01 7465 7374  testdir/two.test
0000010: 6469 722f 7477 6963 655f 6f6e 6500       dir/twice_one.
END
)
    assertEquals "$exp" "$res"
}

test_omitfirst()
{
    res=$($FD -f $D/recursed_a/ $D/recursed_b/ | sortdupes)
    assertEquals 0 $?
    exp=$(sortdupes<<'END'
testdir/recursed_b/three

testdir/recursed_b/one

END
)
    assertEquals "$exp" "$res"
}

. shunit2
