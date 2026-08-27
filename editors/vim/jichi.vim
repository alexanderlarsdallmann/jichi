" jichi.vim - drive the jichi agent from Vim / Neovim.
"
" Mirrors editors/emacs/jichi.el: it pipes buffer/region text plus an instruction
" to `jichi -p -` on stdin (so there is no ARG_MAX limit), reads the
" answer from stdout, and keeps stderr separate. Text commands run --readonly
" (jichi may read the project but not edit it); :JichiTask runs --auto (agentic, may
" edit files) and reloads changed buffers. The subprocess cwd is the buffer's
" project root (git root, else the file's directory).
"
" Install: copy to ~/.vim/plugin/jichi.vim (Vim) or ~/.config/nvim/plugin/jichi.vim
" (Neovim), or load it from your vimrc. Needs the `jichi` binary on PATH
" (override with g:jichi_program). See docs/VIM.md.
"
" Commands:
"   :JichiAsk {question}            ask about the buffer; answer in a scratch split
"   :{range}JichiRegion {instr}     transform the range; answer replaces the range
"   :JichiExplain                   explain the buffer (or range)
"   :JichiTask {task}               agentic --auto run in the project (confirms)
"
" Mappings (set g:jichi_no_mappings = 1 to disable): <Leader>ja ask,
"   <Leader>jr region (visual), <Leader>je explain, <Leader>jt task.

if exists('g:loaded_jlu')
  finish
endif
let g:loaded_jlu = 1

if !exists('g:jichi_program')
  let g:jichi_program = 'jichi'
endif
if !exists('g:jichi_default_args')
  " -q silences stderr diagnostics; the answer is stdout only.
  let g:jichi_default_args = ['-q']
endif

" --- helpers ---------------------------------------------------------------

function! s:root() abort
  let l:dir = expand('%:p:h')
  if l:dir ==# ''
    let l:dir = getcwd()
  endif
  " git root if we're in a repo, else the file's directory.
  let l:git = finddir('.git', l:dir . ';')
  if l:git !=# ''
    return fnamemodify(l:git, ':h')
  endif
  return l:dir
endfunction

function! s:shelljoin(list) abort
  let l:parts = []
  for l:a in a:list
    call add(l:parts, shellescape(l:a))
  endfor
  return join(l:parts, ' ')
endfunction

" Run jichi with `extra_args`, feeding `input` on stdin. Returns the stdout text.
function! s:run(input, extra_args) abort
  let l:args = [g:jichi_program] + g:jichi_default_args + a:extra_args + ['-p', '-']
  let l:cmd = s:shelljoin(l:args)
  let l:cwd = s:root()
  let l:save = getcwd()
  " Run in the project root so the path fence + repo map are project-scoped.
  execute 'lcd' fnameescape(l:cwd)
  try
    let l:out = system(l:cmd, a:input)
  finally
    execute 'lcd' fnameescape(l:save)
  endtry
  if v:shell_error != 0
    echohl ErrorMsg
    echomsg 'jichi: exited ' . v:shell_error . ' (run `:!' . g:jichi_program
          \ . ' doctor` to diagnose)'
    echohl None
  endif
  return l:out
endfunction

" Text of the given line range (1-based inclusive).
function! s:range_text(l1, l2) abort
  return join(getline(a:l1, a:l2), "\n")
endfunction

" Show `text` in a dedicated scratch split (reused across calls).
function! s:show(text) abort
  let l:bufname = '__jlu__'
  let l:winnr = bufwinnr(l:bufname)
  if l:winnr == -1
    execute 'botright new ' . l:bufname
    setlocal buftype=nofile bufhidden=hide noswapfile nowrap
    setlocal filetype=markdown
  else
    execute l:winnr . 'wincmd w'
    setlocal modifiable
    silent %delete _
  endif
  call setline(1, split(a:text, "\n", 1))
  setlocal nomodifiable
  normal! gg
endfunction

" --- commands --------------------------------------------------------------

" Ask a question about the current buffer; answer to the scratch split.
function! s:ask(question) abort
  let l:body = "File: " . expand('%:t') . "\n\n" . join(getline(1, '$'), "\n")
        \ . "\n\n---\nQuestion: " . a:question
  call s:show(s:run(l:body, ['--readonly']))
endfunction

" Transform a line range per an instruction; replace the range with the answer.
function! s:region(l1, l2, instr) abort
  let l:body = "Instruction: " . a:instr
        \ . "\nReturn ONLY the transformed text, no commentary, no code fences.\n\n"
        \ . s:range_text(a:l1, a:l2)
  let l:out = s:run(l:body, ['--readonly'])
  if l:out ==# ''
    return
  endif
  " Strip a trailing newline the shell adds, then replace the range.
  let l:out = substitute(l:out, '\n\+$', '', '')
  let l:lines = split(l:out, "\n", 1)
  execute a:l1 . ',' . a:l2 . 'delete _'
  call append(a:l1 - 1, l:lines)
endfunction

" Agentic run in the project (may edit files). Confirms first; reloads buffers.
function! s:task(task) abort
  if confirm("Run jichi --auto in " . s:root() . "?\n(it may edit project files)",
        \ "&Yes\n&No", 2) != 1
    echo 'jichi: cancelled.'
    return
  endif
  let l:out = s:run(a:task, ['--auto'])
  call s:show(l:out)
  silent! checktime  " reload any buffers jichi changed on disk
endfunction

command! -nargs=1 JichiAsk        call s:ask(<q-args>)
command! -nargs=1 -range JichiRegion call s:region(<line1>, <line2>, <q-args>)
command! -range JichiExplain      call s:region(<line1>, <line2>, 'Explain this code clearly.')
command! -nargs=1 JichiTask       call s:task(<q-args>)

" JichiExplain with no range explains the whole buffer.
command! JichiExplainBuffer call s:ask('Explain what this file does.')

if !exists('g:jichi_no_mappings') || !g:jichi_no_mappings
  nnoremap <silent> <Leader>ja :JichiAsk
  vnoremap <silent> <Leader>jr :JichiRegion
  vnoremap <silent> <Leader>je :JichiExplain<CR>
  nnoremap <silent> <Leader>jt :JichiTask
endif
