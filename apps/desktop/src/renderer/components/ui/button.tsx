import * as React from 'react';
import { cva, type VariantProps } from 'class-variance-authority';
import { cn } from '../../lib/utils';

const buttonVariants = cva(
  'btn',
  {
    variants: {
      variant: {
        default: 'btn--default',
        secondary: 'btn--secondary',
        ghost: 'btn--ghost',
        danger: 'btn--danger',
      },
      size: {
        default: 'btn--md',
        sm: 'btn--sm',
        lg: 'btn--lg',
      },
    },
    defaultVariants: {
      variant: 'default',
      size: 'default',
    },
  },
);

export interface ButtonProps
  extends React.ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {}

export const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, ...props }, ref) => {
    return <button className={cn(buttonVariants({ variant, size }), className)} ref={ref} {...props} />;
  },
);

Button.displayName = 'Button';
