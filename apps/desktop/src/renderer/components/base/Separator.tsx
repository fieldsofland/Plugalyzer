import * as React from 'react';
import * as BaseSeparator from '@base-ui-components/react/separator';

export function Separator(props: React.HTMLAttributes<HTMLElement>): JSX.Element {
  const Root = ((BaseSeparator as any).Separator ?? 'hr') as any;
  return <Root {...props} className={`separator ${props.className ?? ''}`} />;
}
