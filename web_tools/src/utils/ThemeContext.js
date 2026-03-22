import { createContext, useContext } from 'react';
import { DARK } from './theme';

export const ThemeContext = createContext(DARK);
export const useTheme = () => useContext(ThemeContext);
