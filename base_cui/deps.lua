return {
    config = {
        dir = 'deps',
    },
    {
        'timwmillard/cbase',
        subdir = '',
        flatten = true,
        files = { 'lib/base.h' },
    },
    {
        'https://github.com/yappologistic/CUI/releases/download/v1.5.0/cui_single.h',
        name = 'cui-single',
        subdir = '',
        flatten = true,
    },
    {
        'yappologistic/CUI',
    },
}
