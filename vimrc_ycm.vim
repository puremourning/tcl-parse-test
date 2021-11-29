let ycm = expand( '$HOME' ) .. '/.vim/bundle/YouCompleteMe'

let g:ycm_language_server = [
  \   {
  \     'name': 'tcl-parse-test',
  \     'cmdline': [ expand( '<sfile>:p:h' ) .. '/debug-Darwin-arm64/bin/server' ],
  \     'filetypes': [ 'tcl' ],
  \   },
  \ ]

exe "source " . ycm . '/vimrc_ycm_minimal'

