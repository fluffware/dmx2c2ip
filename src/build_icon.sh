
render_file() # svg_file png_file dpi copy
{
    local svg_file=$1
    local out_base=$2
    local size=$3
    local depth=$4
    inkscape "$svg_file" --export-png="${out_base}.png" --export-area-page  --export-width=$size --export-height=$size --without-gui
    pngtopnm -mix ${out_base}.png | pnmdepth $depth >"${out_base}.ppm"
}

icon_file=`dirname "$1"`/`basename "$1" .svg`.ico;
dir=`mktemp --tmpdir -d build_icon.XXXXXXX`;
render_file $1 "$dir/icon64x64" 64 8;
render_file $1 "$dir/icon48x48" 48 4;
render_file $1 "$dir/icon32x32" 32 4;
render_file $1 "$dir/icon16x16" 16 4;

ppmtowinicon -output $icon_file $dir/*.ppm
