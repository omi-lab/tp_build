echo "//Generated by generate_static_init.sh" > $1

echo "#include \"${2}/Globals.h\"" >> $1
echo "extern int ${2}_staticInit;" >> $1
echo "int ${2}_staticInit = []{return ${2}::staticInit();}();" >> $1
