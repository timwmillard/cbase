return {
    config = {
        dir = 'deps',
    },
    {
        'timwmillard/cbase',
        files = { 'lib/base.h' },
        subdir = false,
        flatten = true,
    },
    {
        'eduardsui/tlse',
        tag = 'v1.0.7',
    },
}
