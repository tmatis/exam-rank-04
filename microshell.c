/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   microshell.c                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: tmatis <tmatis@student.42.fr>              +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2021/06/05 15:12:52 by tmatis            #+#    #+#             */
/*   Updated: 2021/06/05 18:25:48 by tmatis           ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <wait.h>


typedef struct s_command
{
	char	**argv;
	int		tube[2];
}	t_command;

typedef struct s_list
{
	void			*data;
	struct s_list	*next;
	struct s_list	*previous;
} 	t_list;

void		puterror(char *str)
{
	int	i;

	i = 0;
	while(str[i])
		i++;
	write(STDERR_FILENO, str, i);
	write(STDERR_FILENO, "\n", 1);
}

int			fatal_error(void)
{
	puterror("error: fatal");
	exit(1);
	return (1);
}

t_list	*lst_new(void *data)
{
	t_list	*new;

	new = malloc(sizeof(t_list));
	if (!new)
		fatal_error();
	new->next = NULL;
	new->previous = NULL;
	new->data = data;
	return (new);
}

void	lst_add(t_list **lst, t_list *new)
{
	t_list	*dup;

	if (!*lst)
	{
		*lst = new;
		return ;
	}
	dup = *lst;
	while (dup->next)
		dup = dup->next;
	new->previous = dup;
	dup->next = new;
}

void	lst_clear(t_list **lst)
{
	t_list		*dup;
	t_list		*temp;
	t_command	*command;

	dup = *lst;
	while (dup)
	{
		temp = dup->next;
		command = (t_command *)dup->data;
		free(command->argv);
		free(dup->data);
		free(dup);
		dup = temp;
	}
	*lst = NULL;
}

int			arg_size(int argc, char **argv)
{
	int	i;

	i = 0;
	while (i < argc && strcmp(argv[i], ";") && strcmp(argv[i], "|"))
		i++;
	return (i);
}

t_command	*parse_command(int argc, char **argv, int *shared_index)
{
	t_command *command;
	int	arg_count;

	command = malloc (sizeof(t_command));
	if (!command)
		fatal_error();
	arg_count = arg_size(argc - *shared_index, argv + *shared_index);
	if (!arg_count)
		fatal_error();
	command->argv = malloc((arg_count + 1) * sizeof(char *));
	if (!command->argv)
		fatal_error();
	int	i = 0;
	while (*shared_index < argc
			&& strcmp(argv[*shared_index], ";")
			&& strcmp(argv[*shared_index], "|"))
		command->argv[i++] = argv[(*shared_index)++];
	command->argv[i] = NULL;
	return (command);
}

void	putstr_fd(char *str, int fd)
{
	int	i = 0;

	while (str[i])
		i++;
	write(fd, str, i);
}

int	builtin(t_command *command)
{
	int	argc = 0;

	while (command->argv[argc])
		argc++;
	if (!strcmp(command->argv[0], "cd"))
	{
		if (argc != 2)
		{
			puterror("error: cd: bad arguments");
			return (1 + 2);
		}
		if (chdir(command->argv[1]) < 0)
		{
			putstr_fd("error: cd: cannot change directory to ", STDERR_FILENO);
			putstr_fd(command->argv[1], STDERR_FILENO);
			putstr_fd("\n", STDERR_FILENO);
			return (1 + 2);
		}
		return (0 + 2);
	}
	else
		return (0);
}

int	exec(t_list *pipe_list, char **envp)
{
	t_command *command;
	t_command *previous;

	command = pipe_list->data;
	if (pipe_list->next)
	{
		if (pipe(command->tube) < 0)
			fatal_error();
	}
	int pid = fork();
	if (pid < 0)
		fatal_error();
	if (pid == 0)
	{
		int	ret;
		if (pipe_list->previous)
		{
			previous = pipe_list->previous->data;
			if (dup2(previous->tube[0], STDIN_FILENO) < 0)
				exit(1);
			close(previous->tube[0]);
		}
		if (pipe_list->next)
		{
			close(command->tube[0]);
			if (dup2(command->tube[1], STDOUT_FILENO) < 0)
				exit(1);
			close(command->tube[1]);
		}
		ret = builtin(command);
		if (ret)
			exit(ret);
		execve(command->argv[0], command->argv, envp);
		putstr_fd("error: cannot execute ", STDERR_FILENO);
		putstr_fd(command->argv[0], STDERR_FILENO);
		putstr_fd("\n", STDERR_FILENO);
		exit(1 + 2);
	}
	else
	{
		int	status = 0;
		if (pipe_list->previous)
		{
			previous = pipe_list->previous->data;
			close(previous->tube[0]);
		}
		if (pipe_list->next)
			close(command->tube[1]);
		waitpid(pid, &status, 0);
		if (WIFEXITED(status))
			return (WEXITSTATUS(status) + 2);
	}
	return (2);
}

int	exec_commands(t_list *pipe_list, char **envp)
{
	int		ret;

	ret = 2;
	while (pipe_list)
	{
		ret = exec(pipe_list, envp);
		if (ret == 1)
			fatal_error();
		pipe_list = pipe_list->next;
	}
	return (ret);
}

int	exec_rules(t_list *pipe_list, char **envp)
{
	int	ret;
	ret = 0;

	if (!pipe_list->next)
		ret = builtin(pipe_list->data);
	if (!ret)
		ret = exec_commands(pipe_list, envp);
	return (ret - 2);
}

int	main(int argc, char **argv, char **envp)
{
	int	shared_index = 1;
	int	ret = 0;
	t_list	*pipes_list = NULL;
	(void)envp;
	if (argc > 1)
	{
		while (shared_index < argc)
		{
			t_command *command;
			command = parse_command(argc, argv, &shared_index);
			lst_add(&pipes_list, lst_new(command));
			if (shared_index == argc || !strcmp(argv[shared_index], ";"))
			{
				ret = exec_rules(pipes_list, envp);
				lst_clear(&pipes_list);
			}
			if (shared_index < argc && (!strcmp(argv[shared_index], "|") || !strcmp(argv[shared_index], ";")))
				shared_index++;
		}
	}
	return (ret);
}