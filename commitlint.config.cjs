module.exports = {
  extends: ['@commitlint/config-conventional'],
  defaultIgnores: false,
  ignores: [],
  rules: {
    'header-max-length': [2, 'always', 100],
    'body-leading-blank': [2, 'always'],
    'footer-leading-blank': [2, 'always'],
    'body-max-line-length': [0],
    'footer-max-line-length': [0],
  },
};
