#ifndef TANTRUMS_STDLIB_FILESYSTEM_H
#define TANTRUMS_STDLIB_FILESYSTEM_H

#include "../runtime.h"

extern "C" {
    TantrumsValue rt_filesystem_read(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_write(TantrumsValue path_tv, TantrumsValue data_tv);
    TantrumsValue rt_filesystem_append(TantrumsValue path_tv, TantrumsValue data_tv);
    TantrumsValue rt_filesystem_exists(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_delete(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_mkdir(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_mkfile(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_listdir(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_isfile(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_isdir(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_copy(TantrumsValue src_tv, TantrumsValue dst_tv);
    TantrumsValue rt_filesystem_move(TantrumsValue src_tv, TantrumsValue dst_tv);
    TantrumsValue rt_filesystem_size(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_readlines(TantrumsValue path_tv);
    TantrumsValue rt_filesystem_writelines(TantrumsValue path_tv, TantrumsValue lines_tv);
    TantrumsValue rt_filesystem_cwd();
    TantrumsValue rt_filesystem_abspath(TantrumsValue path_tv);
}

#endif // TANTRUMS_STDLIB_FILESYSTEM_H
