#!/usr/bin/perl

use v5.12;

$|++;

my @list;
my $i;

my $version = 0;

while (<STDIN>)
{
    s/\s+$//;

    if (/^# *VERSION=(.*)/)
    {
        $version = $1;
    }

    next if /^#/;

    my @fields = split(/,/);

    if (scalar(@fields) > 3)
    {
        $list[int($fields[1])] = 1;

        if ($fields[2] eq "e") {
            print "static const struct MANIFEST_ENUM_DEFINITION MANIFEST_ENTRY_$version","_$fields[1] =\n{\n";
        } elsif ($fields[2] eq "i") {
            print "static const struct MANIFEST_INT_DEFINITION MANIFEST_ENTRY_$version","_$fields[1] =\n{\n";
        } elsif ($fields[2] eq "f") {
            print "static const struct MANIFEST_FLOAT_DEFINITION MANIFEST_ENTRY_$version","_$fields[1] =\n{\n";
        } else {
            die "unexpected field type: ${fields[2]}";
        }
        print "    \"$fields[0]\", $fields[3],";
        given ($fields[2])
        {
            when ("e") {
                my $values = scalar(@fields) - 4;
                print " MANIFEST_TYPE_ENUM, $values,\n    {\n";
                for ($i = 0; $i < $values; ++$i)
                {
                    my ($enum, $value) = split('=', $fields[4 + $i], 2);
                    if (!defined($enum) || !defined($value)) {
                        die "Invalid enum value";
                    }
                    print "        { $enum, \"$value\" },\n";
                }
                print "    }\n";
            }
            when ("i") {
                print " MANIFEST_TYPE_INT, \"$fields[4]\"";
                if ($fields[5] eq "") {
                    print ", 0";
                } else {
                    print ", $fields[5]";
                }
                if ($fields[6] eq "") {
                    print ", 4294967295ul\n";
                } else {
                    print ", $fields[6]\n";
                }
            }
            when ("f") {
                printf " MANIFEST_TYPE_FLOAT, \"%s\", %ff", $fields[4], $fields[5];
                if ($fields[6] eq "") {
                    print ", 0.0f";
                } else {
                    printf ", %ff", $fields[6];
                }
                if ($fields[7] eq "") {
                    print ", 4294967.295f\n";
                } else {
                    printf ", %ff\n", $fields[7];
                }
            }
        }

        print "};\n\n";
    }
}
if ($!)
{
    die "unexpected error while reading from stdin: $!";
}

my $count = scalar(@list);
print "#define MANIFEST_DEFINITION_$version","_COUNT ($count)\n";
print "struct MANIFEST_DEFINITION *MANIFEST_DEFINITIONS_$version","[] = {\n";

for ($i = 0; $i <= $#list; ++$i)
{
    if (defined($list[$i])) {
        print "    (struct MANIFEST_DEFINITION*)&MANIFEST_ENTRY_$version","_$i,\n";
    } else {
        print "    0,\n";
    }
}
print "};\n";
