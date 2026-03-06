import * as React from 'react';
import { cn } from '../../lib/utils';

export function Card(props: React.HTMLAttributes<HTMLDivElement>): JSX.Element {
  const { className, ...rest } = props;
  return <div className={cn('card', className)} {...rest} />;
}

export function CardHeader(props: React.HTMLAttributes<HTMLDivElement>): JSX.Element {
  const { className, ...rest } = props;
  return <div className={cn('card-header', className)} {...rest} />;
}

export function CardTitle(props: React.HTMLAttributes<HTMLHeadingElement>): JSX.Element {
  const { className, ...rest } = props;
  return <h3 className={cn('card-title', className)} {...rest} />;
}

export function CardContent(props: React.HTMLAttributes<HTMLDivElement>): JSX.Element {
  const { className, ...rest } = props;
  return <div className={cn('card-content', className)} {...rest} />;
}
