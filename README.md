[![Build Status](https://travis-ci.org/jesrui/finddupes.svg?branch=master)](https://travis-ci.org/jesrui/finddupes)

# findupes

`findupes` is a UNIX command line utility to list duplicate files in a given
set of directories. Such files are found by comparing file sizes and MD5
signatures. `finddupes` draws heavily from
[`fdupes`](https://github.com/adrianlopezroche/fdupes).

## Examples

### Compare two directory trees and list duplicate files

Duplicate files with the same content are grouped together in sets. By default
files are separated by a new-line character and sets by an empty line.

    finddupes --recursive --noempty --symlinks treeA/ treeB/

In the example above, empty files are skipped and symbolic links are followed.

### Remove all duplicates, asking for confirmation

Let's supose that you want to remove all duplicates in a tree. To choose which
copy to preserve in each duplicate set you would want to confirm each deletion
individually, which sounds like `xargs` could be useful here. The `--separator`
and `--setseparator` options allow you to split the output of `finddupes` with
`xargs`.

NOTE that this works only if filenames do not contain '\n', '\x00' or '\x01'.

    finddupes --recursive --separator '\x01' --setseparator '\x00' someDir |
        xargs -0 -n1 -I{} sh -c '
            s=$(echo {} | tr "\001" "\n"); echo -e "$s"; \
            echo -n {} | tr "\001" "\000" | xargs -0pr -n1 rm'

### Preserve files in a good tree, removing duplicates elsewhere

Say you want to look for dupes in two trees, `goodTree` and `badTree`, and, in
case of duplicates, you want to preserve a copy in `goodTree` and remove any
other dupes.

In this case you can use the `--omitfirst` option, which omits the first match
in each set, and is helpful here because, in each set, files inside the first
tree appear before files inside the second one. The order in which duplicate
sets are printed is not determined though.

NOTE that this removes files without confirmation.

    finddupes --recursive --omitfirst --separator '\x00' --setseparator '\x00' \
        goodTree badTree | xargs -0 rm

### Find out how much disk space is occupied by duplicate files

The same trick with the separator strings can be used to fetch some dupe
statistics. For example, to find out how much disk space could be liberated by
removing dupes:

    finddupes --recursive --separator '\x01' --setseparator '\x00' --omitfirst someDir/ |
        xargs -0 -n1 -I{} sh -c '
            echo -n {} | tr "\001" "\000" | xargs -0r du -csh'

Or, more compact, with a grand total but without detailing the usage of each
set:

    finddupes --recursive --separator '\x00' --setseparator '\x00' --omitfirst someDir/ |
        xargs -0 du -ch 

### List files without duplicates whose contents are present in tree A but not in tree B

The `--unique` flag makes `finddupes` list only files that don't have
duplicates.

    finddupes --recursive --unique treeA treeB | grep '^treeA/'

## License

finddupes is distributed under the
[MIT license](http://opensource.org/licenses/MIT).
